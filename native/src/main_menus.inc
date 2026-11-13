// N7: the interactive shell with menus, settings, the N7 HUD and sound. Included by main.cpp inside
// main(), after the window and the renderer exist (it uses main()'s map loading and session helpers).
// The automation paths (--frames, the N3-N6.5 self-tests, captures) do not come here.
{
    using cat::ui::Command;
    using cat::ui::MenuEvent;
    using cat::ui::Screen;
    SDL_SetWindowTitle(window, "Cat Arena: Feral Claw");
    if(!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) cat::log("warning", "input", std::string("Gamepads unavailable: ") + SDL_GetError());
    // ---- settings (the legacy localStorage keys, one "id=value" line each)
    std::filesystem::path settings_path;
    if(game_settings_file) settings_path = *game_settings_file;
    else if(menu_selftest || coop_selftest) { settings_path = std::filesystem::temp_directory_path() / "cat_arena_menu_selftest_settings.txt"; std::error_code ec; std::filesystem::remove(settings_path, ec); }
    else {
        char* pref = SDL_GetPrefPath("CatArena", "FeralClaw");
        settings_path = pref ? cat::utf8_path(pref) / "settings.txt" : base / "settings.txt";
        SDL_free(pref);
    }
    cat::GameSettings gs;
    if(std::ifstream in(settings_path); in) { gs = cat::GameSettings::load(in); cat::log("info", "settings", "Loaded " + cat::path_utf8(settings_path)); }
    else cat::log("info", "settings", "No saved settings yet: legacy defaults (" + cat::path_utf8(settings_path) + ")");
    if(map_given) gs.map = map;
    int saves = 0;
    auto save_settings = [&] {
        std::ofstream out(settings_path);
        gs.save(out);
        if(out) ++saves; else cat::log("warning", "settings", "Cannot write " + cat::path_utf8(settings_path));
    };
    // ---- menus and HUD
    const auto n7 = n7_asset_dir(root);
    if(n7.empty()) throw std::runtime_error("assets/n7 (menus, fonts and sounds) not found next to the map assets");
    cat::Stopwatch ui_clock;
    UiKit kit = load_ui_kit(n7);
    renderer.load_ui(kit.atlas.image());
    cat::log("info", "ui", "UI atlas: " + std::to_string(kit.atlas.glyph_count()) + " glyphs (Liberation Serif Regular / Bold, Sans Bold, Mono Bold + DejaVu Sans Bold symbols) and the menu backdrop in " +
                               std::to_string(static_cast<int>(std::chrono::duration<double, std::milli>(ui_clock.lap()).count())) + " ms");
    cat::ui::Menu menu(gs, menu_maps());
    // ---- N8 multiplayer: the relay connection (cat::net::Coop), the built-in relay (HOST ON THIS PC), the server list
    cat::net::Coop coop;
    std::optional<cat::net::RelayServer> local_relay;
    double relay_stop_at = -1;
    std::future<cat::net::ServerList> browse;
    double next_browse_ms = 0;
    bool wipe_pending = false, restart_pending = false, text_input_on = false;
    cat::ui::MultiplayerView mpv;
    std::string shell_status; bool shell_error = false;   // messages of the shell itself (built-in relay errors, cancel)
    Screen connect_from = Screen::multiplayer;
    const auto t_net = std::chrono::steady_clock::now();
    auto net_now = [&] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_net).count(); };
    auto map_of_wire = [](const std::string& wire) -> const MapEntry* { for(const auto& m : kMaps) if(wire == m.wire_id) return &m; return nullptr; };
    auto detach_coop = [&] { coop.attach(nullptr, nullptr); };
    // ---- sound: mixer on the device thread, legacy sample bank and the ASHEN DRIVE theme
    cat::audio::Mixer mixer(48000);
    std::optional<cat::audio::Device> device;
    auto open_audio = [&] {
        device.emplace(mixer);
        cat::log(device->ok() ? "info" : "warning", "audio", device->ok() ? "Audio: " + device->status() : "Silent: " + device->status());
    };
    open_audio();
    bool reconnect_audio = false;
    auto bank = std::make_shared<const cat::audio::SampleBank>(cat::audio::SampleBank::load(n7 / "audio" / "sfx", mixer.rate()));
    cat::audio::GameAudio audio(mixer, bank);
    mixer.set_music(cat::audio::open_mp3(cat::audio::read_file(n7 / "audio" / "main-theme.mp3")));
    cat::log("info", "audio", "Sound bank and soundtrack from " + cat::path_utf8(n7 / "audio") + " in " + std::to_string(static_cast<int>(std::chrono::duration<double, std::milli>(ui_clock.lap()).count())) + " ms");
    // ---- input state
    cat::InputModeTracker modes; cat::PointerCapture pointer; cat::TouchControls touch; cat::SprintLatch sprint; cat::PerfMeter perf;
    std::vector<SDL_Gamepad*> pads;
    std::array<double, 6> axes{};   // left x / y, right x / y, left / right trigger (-1..1, 0..1)
    std::uint32_t keys = 0, taps = 0, pad_keys = 0;
    std::array<bool, SDL_SCANCODE_COUNT> held_keys{};
    std::string keyboard_layout, mouse_fire;
    double mouse_sensitivity = 1, mouse_y = 1;
    bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    pointer.set_focused(focused);
    cat::DesktopDisplay display(window, settings.width, settings.height);
    bool shift_held = false, pad_sprint = false, relative_on = false, fullscreen_launch_done = false;
    std::optional<SDL_FingerID> menu_finger;
    std::vector<cat::ui::HudElement> hud_elements;
    double pointer_x = -1, pointer_y = -1;
    // ---- settings applied to the renderer, the session and the mixer
    bool vsync_now = settings.vsync; int frame_cap = settings.frame_cap; int applied = 0;
    auto apply_settings = [&] {
        if(!fov_given) fov = gs.number("fovControl");
        display.configure(gs.text("displayResolution"), static_cast<int>(gs.number("displayRefresh")));
        const auto new_layout = gs.text("keyboardLayout"), new_fire = gs.text("mouseFire");
        if(new_layout != keyboard_layout || new_fire != mouse_fire) {
            keys = taps = 0;
            held_keys.fill(false);
        }
        keyboard_layout = new_layout; mouse_fire = new_fire;
        mouse_sensitivity = gs.number("mouseSensitivity") / 100;
        mouse_y = gs.flag("mouseInvertY") ? -1 : 1;
        SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, gs.flag("mouseRaw") ? "0" : "1");
        features.dynamic_lights = gs.flag("gfxDynamicLights"); features.clouds = gs.flag("gfxSky"); features.culling = gs.flag("gfxVis"); features.fog = gs.number("fogQuality") > 0;
        light_grid = gs.flag("gfxEntityGrid"); shadow_mode = std::stoi(gs.text("gfxShadows"));
        if(session) {
            session->fov = fov; session->light_grid = light_grid; session->enemy_options.shadows = shadow_mode;
            session->effects.set_viewmodel({gs.number("vmAngleOffset"), gs.number("vmSizeFov"), gs.number("vmVerticalOffset")});
            session->arena.player().view().bob_scale = gs.number("motionStrength") / 100.0;
        }
        if(gs.flag("vsyncEnabled") != vsync_now && !no_vsync) { vsync_now = !vsync_now; renderer.set_present_mode(vsync_now); }
        const double limit = gs.number("fpsLimit");
        frame_cap = cap ? *cap : limit >= 241 ? 0 : std::max(10, static_cast<int>(limit));
        cat::audio::MixSettings mix;
        mix.master = static_cast<int>(gs.number("masterVolume")); mix.weapons = static_cast<int>(gs.number("weaponVolume")); mix.enemies = static_cast<int>(gs.number("enemyVolume"));
        mix.music = static_cast<int>(gs.number("musicVolume")); mix.music_enabled = gs.flag("musicEnabled");
        audio.apply(mix);
        modes.set_setting(gs.text("inputMode")); sprint.set_mode(gs.text("sprintMode"));
        ++applied;
    };
    // ---- maps: render-only preview for the maps page, staged loading for a game
    std::string scene_map;   // the map whose render scene the renderer holds
    auto show_scene = [&](const std::string& id) {
        if(scene_map == id) return;
        detach_coop(); player = nullptr; session.reset();
        load_render_scene(id);
        renderer.load_scene(*scene);
        attach_light_volume(renderer);
        scene_map = id;
    };
    // After a session is created in a server: guests spawn near the host's start (netFindValidSpawn); the
    // network layer gets the arena (host: authoritative; guest: enemies become the host's puppets).
    auto coop_attach_session = [&] {
        if(!coop.in_room() || !session) return;
        if(coop.mode() == cat::net::Coop::Mode::guest && collision && !collision->spawns().empty()) {
            const auto& sp = collision->spawns().front();
            const auto at = coop.find_spawn(*collision, sp.x, sp.y, sp.z, coop.slot());
            session->arena.spawn(at[0], at[1], at[2], sp.a, 0, session->arena.now());
        }
        coop.attach(&session->arena, session->horde ? &*session->horde : nullptr);
    };
    // Squad wipe (host) or the host's 'restart' (guest): the same map again from wave 1, no loading screen
    // (legacy netRestartRound: resetGame on the loaded map).
    auto restart_round = [&] {
        if(!session || !collision) return;
        detach_coop(); player = nullptr; session.reset();
        spawn_index = 0; start_session(); session->show_hud = false; apply_settings();
        coop_attach_session();
        if(coop.mode() == cat::net::Coop::Mode::host) coop.round_restarted();
        session->notice = "SQUAD ELIMINATED · WAVE 1 RESTARTS"; session->notice_time = 3;
        cat::log("info", "net", "Round restarted (squad wipe)");
    };
    struct { bool active{}; std::string map; int phase{}; double ms{}; std::string error; } load;
    auto loading_state = [&] {
        cat::ui::LoadingState s; s.map_title = find_map(load.map)->title; s.phase = load.phase; s.ready = load.active && load.phase >= 7; s.error = load.error; s.millis = load.ms;
        char b[96]; std::snprintf(b, sizeof b, "NATIVE · %.0f MS", load.ms); s.info = b;
        return s;
    };
    bool overlay_ready = false;
    auto begin_loading = [&](const std::string& id) {
        detach_coop(); player = nullptr; session.reset();
        load = {}; load.active = true; load.map = id; gs.map = id; menu.select_map(id);
        menu.show_loading(loading_state());
        audio.set_in_game(false);
        keys = taps = 0; touch.reset(); sprint.reset();
    };
    auto loading_step = [&] {   // one stage per frame, so that the loading screen shows each one
        if(!load.active || load.phase >= 7 || !load.error.empty()) return;
        cat::Stopwatch t;
        try {
            switch(load.phase) {
                case 0: if(scene_map != load.map) { scene_map.clear(); load_render_scene(load.map); } else map = load.map; break;   // GENERATE MAP BLUEPRINT
                case 1: if(scene_map != load.map) { renderer.load_scene(*scene); scene_map = load.map; } break;                       // PREPARE TERRAIN
                case 2: if(!collision) load_collision(); break;                                                                       // COMPILE COLLISION
                case 3: if(!nav_graph) build_navigation(); break;                                                                     // BUILD ENEMY NAVIGATION
                case 4: if(!overlay_ready && overlay_enabled) overlay_ready = attach_overlay(renderer); else attach_light_volume(renderer); break;   // BAKE GEOMETRY + LIGHTING
                case 5: spawn_index = 0; start_session(); session->show_hud = false; apply_settings(); coop_attach_session(); break;   // PLACE ENEMIES + PICKUPS
                case 6: break;                                                                                                        // WARM UP GRAPHICS (this frame draws the arena)
                default: break;
            }
            load.ms += std::chrono::duration<double, std::milli>(t.lap()).count();
            ++load.phase;
        } catch(const std::exception& e) {
            load.error = e.what();
            cat::log("error", "scene", std::string("Loading ") + load.map + " failed: " + e.what());
            detach_coop(); player = nullptr; session.reset();
        }
        menu.update_loading(loading_state());
        if(load.phase >= 7) cat::log("info", "app", "Loaded " + load.map + " in " + std::to_string(static_cast<int>(load.ms)) + " ms: waiting for ENTER THE ARENA");
    };
    auto stop_game = [&] { detach_coop(); player = nullptr; session.reset(); load = {}; audio.set_in_game(false); keys = taps = 0; touch.reset(); };
    // N8: leaving a server (pause menu, disconnection, cancel): close the connection and the built-in relay
    auto leave_server = [&](const std::string& why = {}) {
        coop.leave(why);
        // The built-in relay stops once the guests have gone after its 'host_left' (server.js closeRoom), at most 3 s
        // later: closing their sockets at once could discard that last message.
        if(local_relay && relay_stop_at < 0) relay_stop_at = net_now() + 3000;
        wipe_pending = restart_pending = false;
    };
    auto start_browse = [&] {
        if(browse.valid() || coop.busy()) return;
        mpv.listing = true; mpv.list_status = "Checking the server list...";
        browse = std::async(std::launch::async, [relay = gs.relay] { return cat::net::fetch_servers(relay, 23000); });
        next_browse_ms = net_now() + 9000;
    };
    auto start_hosting = [&](const std::string& map_id, bool on_this_pc) {
        if(coop.busy()) return;
        const auto* m = find_map(map_id);
        if(!m) return;
        std::string relay = gs.relay;
        if(on_this_pc) {   // native addition: the relay of reference/multiplayer/server.js inside the game, on every network interface
            relay_stop_at = -1;   // still running after the last server: reuse it
            if(!local_relay) {
                local_relay.emplace();
                local_relay->set_log([](const std::string& line) { cat::log("info", "relay", line); });
                std::string error;
                if(!local_relay->start("0.0.0.0", cat::net::kDefaultPort, error)) {
                    local_relay.reset();
                    shell_status = "Cannot start the built-in server on port " + std::to_string(cat::net::kDefaultPort) + ": " + error + ". Is another Cat Arena server running? Use CREATE SERVER with its address instead.";
                    shell_error = true;
                    return;
                }
                cat::log("info", "net", "Built-in relay listening on port " + std::to_string(local_relay->port()));
            }
            relay = "ws://127.0.0.1:" + std::to_string(local_relay->port()) + "/ws";
        }
        save_settings();
        shell_status.clear();
        coop.host(relay, gs.player_name, gs.server_name, m->wire_id);
        if(coop.busy()) { connect_from = Screen::maps; menu.show_connecting(); }
    };
    auto set_fullscreen = [&](bool on) { display.fullscreen(on); };
    auto is_fullscreen = [&] { return display.fullscreen(); };
    auto run = [&](const std::vector<Command>& commands) {
        for(const auto& c : commands) {
            switch(c.kind) {
                case Command::Kind::preview_map:
                    audio.start();   // audiostart(): PLAY is a deliberate action
                    try { show_scene(c.arg); } catch(const std::exception& e) { cat::log("error", "scene", std::string("Preview of ") + c.arg + " failed: " + e.what()); }
                    break;
                case Command::Kind::start_game: audio.start(); save_settings(); begin_loading(c.arg); break;
                case Command::Kind::enter_arena:
                    if(!session) break;
                    audio.start(); menu.close(); load.active = false;
                    audio.set_in_game(true);
                    pointer.set_game_active(true); pointer.set_menu_open(false); pointer.request();   // the click on ENTER THE ARENA is the user gesture
                    if(gs.flag("autoFullscreen") && !fullscreen_launch_done && !is_fullscreen() && !menu_selftest && !coop_selftest) { set_fullscreen(true); fullscreen_launch_done = true; }
                    timer.lap();
                    break;
                case Command::Kind::resume: pointer.set_menu_open(false); pointer.request(); timer.lap(); break;
                case Command::Kind::return_to_menu: if(coop.busy()) leave_server(); stop_game(); menu.show_home(); break;
                case Command::Kind::change_map:
                    stop_game(); menu.show_maps();
                    try { show_scene(menu.selected_map()); } catch(const std::exception& e) { cat::log("error", "scene", std::string("Preview failed: ") + e.what()); }
                    break;
                case Command::Kind::quit: running = false; break;
                case Command::Kind::settings_changed: apply_settings(); save_settings(); break;
                case Command::Kind::test_audio: audio.start(); audio.test(); break;
                case Command::Kind::fullscreen_now: set_fullscreen(!is_fullscreen()); break;
                case Command::Kind::debug_view: features.debug_view = (features.debug_view + 1) % 4; break;
                case Command::Kind::hud_editor_done: case Command::Kind::reset_hud: save_settings(); break;
                case Command::Kind::host_server: audio.start(); start_hosting(c.arg, false); break;
                case Command::Kind::host_local: audio.start(); start_hosting(c.arg, true); break;
                case Command::Kind::refresh_servers: start_browse(); break;
                case Command::Kind::join_server: {
                    audio.start();
                    std::string wire;
                    for(const auto& r : mpv.rooms) if(r.id == c.arg) for(const auto& m : kMaps) if(r.map_title == m.title) wire = m.wire_id;
                    save_settings();
                    shell_status.clear();
                    coop.join(gs.relay, c.arg, gs.player_name, wire);
                    if(coop.busy()) { connect_from = Screen::servers; menu.show_connecting(); }
                    break;
                }
                case Command::Kind::cancel_connect: leave_server(); shell_status = "Connection cancelled."; shell_error = false; break;
                case Command::Kind::leave_room: leave_server(); stop_game(); menu.show_home(); break;
                case Command::Kind::form_changed: break;   // saved with the next host / join / exit
            }
        }
    };
    apply_settings();
    if(map_given) begin_loading(map); else menu.show_home();
    // ---- --menu-selftest: synthetic key events through the normal event path, one step per frame
    struct {
        int stage{}, presses{}, waited{};
        bool home_ok{}, preview_ok{}, loaded_ok{}, playing_ok{}, fired_ok{}, paused_ok{}, settings_ok{}, fov_ok{}, volume_ok{}, returned_ok{}, quit_ok{};
        double t0{}, paused_at{};
        std::string failure;
    } mt;
    auto push_key = [&](SDL_Scancode code, SDL_Keymod mod = SDL_KMOD_NONE) {
        for(bool down : {true, false}) { SDL_Event e{}; e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP; e.key.windowID = SDL_GetWindowID(window); e.key.scancode = code; e.key.mod = mod; e.key.down = down; SDL_PushEvent(&e); }
    };
    auto push_button = [&](bool down) {
        SDL_Event e{}; e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP; e.button.windowID = SDL_GetWindowID(window); e.button.button = SDL_BUTTON_LEFT; e.button.down = down; SDL_PushEvent(&e);
    };
    auto selftest_step = [&] {
        auto fail = [&](const std::string& why) { if(mt.failure.empty()) mt.failure = why; mt.stage = 99; running = false; };
        auto focus_to = [&](const std::string& id, SDL_Scancode key = SDL_SCANCODE_DOWN) {   // one arrow press per frame until `id` has focus
            if(menu.focused() == id) { mt.presses = 0; return true; }
            if(++mt.presses > 80) fail("focus never reached " + id + " (stuck at " + menu.focused() + ")");
            else push_key(key);
            return false;
        };
        const double now = session ? session->arena.now() : 0;
        switch(mt.stage) {
            case 0: if(++mt.waited < 3) break;
                mt.home_ok = menu.screen() == Screen::home && menu.focused() == "home.play";
                push_key(SDL_SCANCODE_RETURN); mt.stage = 1; break;
            case 1: if(menu.screen() != Screen::maps) { if(++mt.waited > 200) fail("PLAY did not open the maps page"); break; }
                mt.preview_ok = scene_map == menu.selected_map();
                if(focus_to("maps.enter")) { push_key(SDL_SCANCODE_RETURN); mt.stage = 2; mt.waited = 0; }
                break;
            case 2: if(menu.screen() != Screen::loading || menu.focused() != "loading.enter") { if(++mt.waited > 600) fail("loading never became ready"); break; }
                mt.loaded_ok = session.has_value() && load.phase >= 7;
                push_key(SDL_SCANCODE_RETURN); mt.stage = 3; mt.waited = 0; break;
            case 3: if(menu.active() || !session) { if(++mt.waited > 60) fail("ENTER THE ARENA did not start the game"); break; }
                if(!relative_on) { if(++mt.waited > 240) fail("the mouse capture requested by ENTER THE ARENA was not granted (window without keyboard focus?)"); break; }
                mt.t0 = now; push_button(true); mt.stage = 4; mt.waited = 0; break;
            case 4: if(++mt.waited < 90) break;
                push_button(false);
                mt.playing_ok = now > mt.t0 + .3; mt.fired_ok = session->volleys >= 1 && audio.counts().guns >= 1;
                push_key(SDL_SCANCODE_ESCAPE); mt.stage = 5; mt.waited = 0; break;
            case 5: if(++mt.waited == 10) mt.paused_at = now;
                if(mt.waited < 40) break;
                mt.paused_ok = menu.screen() == Screen::pause && now == mt.paused_at && !relative_on;
                if(focus_to("pause.settings")) { push_key(SDL_SCANCODE_RETURN); mt.stage = 6; mt.waited = 0; }
                break;
            case 6: if(menu.screen() != Screen::settings) { fail("SETTINGS did not open from the pause menu"); break; }
                if(menu.settings_tab() != 0) { push_key(SDL_SCANCODE_TAB, static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_LSHIFT)); break; }   // Ctrl+Shift+Tab to VIEW & CAMERA
                if(focus_to("set.fovControl")) { mt.stage = 7; mt.waited = 0; }
                break;
            case 7: if(mt.waited < 5) { push_key(SDL_SCANCODE_RIGHT); ++mt.waited; break; }   // five steps of 1 degree
                mt.fov_ok = gs.number("fovControl") == 110 && fov == 110;
                mt.stage = 8; break;
            case 8: if(menu.settings_tab() != 4) { push_key(SDL_SCANCODE_PAGEDOWN); break; }   // PageDown to AUDIO
                if(focus_to("set.masterVolume")) { mt.stage = 9; mt.waited = 0; }
                break;
            case 9: if(mt.waited < 10) { push_key(SDL_SCANCODE_LEFT); ++mt.waited; break; }
                mt.volume_ok = gs.number("masterVolume") == 60;
                mt.settings_ok = mt.fov_ok && mt.volume_ok && applied > 2;
                push_key(SDL_SCANCODE_ESCAPE); mt.stage = 10; break;
            case 10: if(menu.screen() != Screen::pause) { fail("Esc did not return from the settings to the pause menu"); break; }
                if(focus_to("pause.return")) { push_key(SDL_SCANCODE_RETURN); mt.stage = 11; }
                break;
            case 11: mt.returned_ok = menu.screen() == Screen::home && !session;
                push_key(SDL_SCANCODE_ESCAPE); mt.stage = 12; break;   // catBack on the home page asks to exit
            case 12: if(menu.screen() != Screen::exit_confirm) { fail("Esc on the home page did not ask to exit"); break; }
                if(focus_to("exit.confirm", SDL_SCANCODE_UP)) { push_key(SDL_SCANCODE_RETURN); mt.stage = 13; mt.quit_ok = true; }
                break;
            default: break;
        }
    };
    // ---- --coop-selftest (N8): host through the menus with synthetic keys (MULTIPLAYER, HOST A SERVER, a typed
    // player name, HOST ON THIS PC, ENTER THE ARENA), then three guests in this process join over TCP through the
    // built-in relay, each with its own simulation; checks the host's world, snapshots, name tags, revive, the
    // game menu that does not pause a server, and leaving.
    struct CoopBot {
        std::string name;
        std::unique_ptr<cat::combat::Arena> arena;
        std::unique_ptr<cat::combat::Horde> horde;
        cat::net::Coop coop;   // last: destroyed first, while the arena it points to still exists
        std::uint32_t keys{};
        bool joined{}, host_left{};
        std::uint64_t ticks{};
    };
    std::vector<std::unique_ptr<CoopBot>> bots;
    struct {
        int stage{}, presses{}, waited{};
        bool menus_ok{}, typed_ok{}, hosting_ok{}, listed_ok{}, joined_ok{}, world_ok{}, marines_ok{}, tags_ok{}, damage_ok{}, revive_ok{}, menu_runs_ok{}, left_ok{}, quit_ok{};
        double t0{}, paused_at{};
        std::uint64_t host_ticks{};
        std::size_t tags{}, marines{};
        std::string failure, room;
    } ct;
    std::size_t coop_tags_drawn = 0;
    auto bot_step_all = [&] {   // the guests run as many 120 Hz ticks as the host
        const std::uint64_t host = simulation.ticks();
        for(auto& b : bots) {
            b->coop.poll(net_now());
            for(auto& ev : b->coop.take_events()) {
                if(ev.kind == cat::net::CoopEvent::Kind::ready) b->joined = true;
                if(ev.kind == cat::net::CoopEvent::Kind::disconnected) { cat::log("info", "app", "Self-test guest " + b->name + ": " + ev.text); b->host_left = b->host_left || ev.text.find("Host disconnected") != std::string::npos; }
            }
            if(!b->arena) continue;
            for(; b->ticks < host; ++b->ticks) { b->arena->step({b->keys}); b->coop.after_step(); }
        }
    };
    auto coop_step = [&] {
        auto fail = [&](const std::string& why) { if(ct.failure.empty()) ct.failure = why; ct.stage = 99; running = false; };
        auto focus_to = [&](const std::string& id, SDL_Scancode key = SDL_SCANCODE_DOWN) {
            if(menu.focused() == id) { ct.presses = 0; return true; }
            if(++ct.presses > 80) fail("focus never reached " + id + " (stuck at " + menu.focused() + ")"); else push_key(key);
            return false;
        };
        bot_step_all();
        const double now = session ? session->arena.now() : 0;
        auto mate_of = [&](const CoopBot& b) -> cat::combat::Teammate* {
            for(const auto& p : coop.players()) if(p.id == b.coop.self_id() && session) return session->arena.teammate(p.key);
            return nullptr;
        };
        switch(ct.stage) {
            case 0: if(++ct.waited < 3) break;
                if(focus_to("home.multi")) { push_key(SDL_SCANCODE_RETURN); ct.stage = 1; ct.waited = 0; }
                break;
            case 1: if(menu.screen() != Screen::multiplayer) { if(++ct.waited > 60) fail("MULTIPLAYER did not open"); break; }
                if(focus_to("multi.host")) { push_key(SDL_SCANCODE_RETURN); ct.stage = 2; ct.waited = 0; }
                break;
            case 2: if(menu.screen() != Screen::maps || !menu.host_mode()) { if(++ct.waited > 60) fail("HOST A SERVER did not open the host's map page"); break; }
                ct.menus_ok = true;
                if(focus_to("maps.map.reactor-bastion")) { push_key(SDL_SCANCODE_RETURN); ct.stage = 3; }
                break;
            case 3: if(focus_to("mp.name")) {   // erase the name and type a new one through SDL_EVENT_TEXT_INPUT
                    for(int i = 0; i < 24; ++i) push_key(SDL_SCANCODE_BACKSPACE);
                    SDL_Event e{}; e.type = SDL_EVENT_TEXT_INPUT; e.text.windowID = SDL_GetWindowID(window); e.text.text = "Selftest Host"; SDL_PushEvent(&e);
                    ct.stage = 4; ct.waited = 0;
                }
                break;
            case 4: if(++ct.waited < 3) break;
                ct.typed_ok = gs.player_name == "Selftest Host" && menu.screen() == Screen::maps;
                if(focus_to("host.local")) { push_key(SDL_SCANCODE_RETURN); ct.stage = 5; ct.waited = 0; }
                break;
            case 5: if(menu.screen() != Screen::loading || menu.focused() != "loading.enter") { if(++ct.waited > 900) fail("hosting / loading never became ready: " + coop.status()); break; }
                ct.hosting_ok = coop.mode() == cat::net::Coop::Mode::host && local_relay && !coop.room_code().empty() && map == "reactor-bastion";
                push_key(SDL_SCANCODE_RETURN); ct.stage = 6; ct.waited = 0; break;
            case 6: {
                if(menu.active() || !session) { if(++ct.waited > 120) fail("ENTER THE ARENA did not start the game"); break; }
                const auto list = cat::net::fetch_servers("ws://127.0.0.1:" + std::to_string(cat::net::kDefaultPort) + "/ws", 5000);
                ct.listed_ok = list.ok && list.rooms.size() == 1 && list.rooms[0].id == coop.room_code() && list.rooms[0].host == "Selftest Host" && list.rooms[0].players == 1;
                if(!list.rooms.empty()) ct.room = list.rooms[0].id;
                for(const char* n : {"Ash", "Brick", "Cinder"}) {
                    bots.push_back(std::make_unique<CoopBot>()); bots.back()->name = n;
                    bots.back()->coop.join("127.0.0.1:" + std::to_string(cat::net::kDefaultPort), ct.room, n, "reactor-bastion-v1");
                }
                ct.stage = 7; ct.waited = 0; break;
            }
            case 7: {
                bool all = true;
                for(auto& b : bots) all = all && b->joined;
                if(!all || session->arena.team().size() != 3) { if(++ct.waited > 600) fail("the guests did not join (" + std::to_string(session->arena.team().size()) + " teammates)"); break; }
                ct.joined_ok = coop.badge() == "HOSTING · 4/4 PLAYERS";
                // each guest's own simulation, in front of the host player 4 / 6.5 / 9 m away (else netFindValidSpawn)
                const auto& pl = session->arena.player().state();
                std::uint32_t seed = 101;
                double d = 4;
                for(auto& b : bots) {
                    b->arena = std::make_unique<cat::combat::Arena>(*collision, seed++);
                    double x = pl.x + std::cos(pl.a) * d, y = pl.y + std::sin(pl.a) * d, z = pl.z;
                    const auto sup = collision->support(x, y, pl.z + .5, .6);
                    if(sup && collision->capsule_clear(x, y, sup->z, cat::physics::kPlayerHeight, cat::physics::kPlayerRadius) && !std::isfinite(collision->shot_collision_t(pl.x, pl.y, pl.z + 1.5, x, y, sup->z + 1.2, .1))) z = sup->z;
                    else { const auto at = b->coop.find_spawn(*collision, collision->spawns().front().x, collision->spawns().front().y, collision->spawns().front().z, b->coop.slot()); x = at[0]; y = at[1]; z = at[2]; }
                    b->arena->spawn(x, y, z, pl.a + 3.14159, 0, 0);
                    b->arena->set_loadout(0, 1);
                    b->horde = std::make_unique<cat::combat::Horde>(*b->arena, *nav_graph);
                    b->arena->attach(b->horde.get());
                    b->coop.attach(b->arena.get(), b->horde.get());
                    b->ticks = simulation.ticks();
                    d += 2.5;
                }
                ct.host_ticks = simulation.ticks(); ct.stage = 8; ct.waited = 0; break;
            }
            case 8: {   // 2 s of play: snapshots on the guests, Marine Cats and name tags on the host
                if(simulation.ticks() < ct.host_ticks + 240) break;
                bool same = true;
                for(auto& b : bots) same = same && b->horde->wave_number() == session->horde->wave_number() && b->arena->targets().size() == session->arena.targets().size() && b->coop.stats().last_world_age_ms >= 0 && b->coop.stats().last_world_age_ms < 1000;
                ct.world_ok = same && !session->arena.targets().empty();
                ct.marines = session->enemy_frame.marines.size(); ct.marines_ok = ct.marines >= 1;
                ct.tags = coop_tags_drawn; ct.tags_ok = ct.tags >= 1;
                // freeze the wave from here: only the test deals damage
                session->arena.clear_targets(); session->horde->stage_wave(1, 50, 50, 0); session->god = true;
                ct.host_ticks = simulation.ticks(); ct.stage = 9; break;
            }
            case 9: {   // guest 1 is downed by the host's simulation, guest 2 stands next to it and holds F
                if(simulation.ticks() < ct.host_ticks + 120) break;
                cat::combat::Teammate* m = mate_of(*bots[0]);
                if(!m) { fail("no teammate for guest 1 on the host"); break; }
                session->arena.remote_damage(*m, 500);
                ct.host_ticks = simulation.ticks(); ct.stage = 10; break;
            }
            case 10: {
                if(simulation.ticks() < ct.host_ticks + 60) break;
                ct.damage_ok = bots[0]->arena->player().state().downed && bots[0]->arena->health() == 0;
                auto& p2 = bots[1]->arena->player().state();
                const auto& p1 = bots[0]->arena->player().state();
                p2.x = p1.x - std::cos(p1.a) * .9; p2.y = p1.y - std::sin(p1.a) * .9; p2.z = p1.z; p2.vx = p2.vy = 0;
                bots[1]->keys = cat::combat::key_revive;
                ct.host_ticks = simulation.ticks(); ct.stage = 11; break;
            }
            case 11:
                if(!bots[0]->arena->player().state().downed) { ct.revive_ok = bots[0]->arena->health() == 45 && simulation.ticks() >= ct.host_ticks + 470; bots[1]->keys = 0; push_key(SDL_SCANCODE_ESCAPE); ct.stage = 12; ct.waited = 0; break; }
                if(simulation.ticks() > ct.host_ticks + 1200) fail("guest 1 was not revived in 10 s (" + bots[1]->coop.revive_status() + ")");
                break;
            case 12: if(++ct.waited == 10) ct.paused_at = now;
                if(ct.waited < 60) break;
                ct.menu_runs_ok = menu.screen() == Screen::pause && now > ct.paused_at + .3;
                if(focus_to("pause.leave")) { push_key(SDL_SCANCODE_RETURN); ct.stage = 13; ct.waited = 0; }
                break;
            case 13: {
                bool told = true;
                for(auto& b : bots) told = told && b->host_left;
                if(!told) { if(++ct.waited > 600) fail("the guests were not told that the host left"); break; }
                if(local_relay) { if(++ct.waited > 600) fail("the built-in relay did not stop after the guests left"); break; }
                ct.left_ok = menu.screen() == Screen::home && !session && !coop.busy();
                push_key(SDL_SCANCODE_ESCAPE); ct.stage = 14; break;
            }
            case 14: if(menu.screen() != Screen::exit_confirm) { fail("Esc on the home page did not ask to exit"); break; }
                if(focus_to("exit.confirm", SDL_SCANCODE_UP)) { push_key(SDL_SCANCODE_RETURN); ct.stage = 15; ct.quit_ok = true; }
                break;
            default: break;
        }
    };
    // ---- the loop
    const auto t_start = std::chrono::steady_clock::now();
    auto pixels = [&](double wx, double wy) { const double d = SDL_GetWindowPixelDensity(window); return std::pair{wx * d, wy * d}; };
    auto game_bit = [&](SDL_Scancode code) -> std::uint32_t {   // the legacy key codes, by physical key
        using namespace cat::physics;
        const auto forward = keyboard_layout == "zqsd" ? SDL_SCANCODE_Z : keyboard_layout == "arrows" ? SDL_SCANCODE_UP : SDL_SCANCODE_W;
        const auto left = keyboard_layout == "zqsd" ? SDL_SCANCODE_Q : keyboard_layout == "arrows" ? SDL_SCANCODE_LEFT : SDL_SCANCODE_A;
        const auto back = keyboard_layout == "arrows" ? SDL_SCANCODE_DOWN : SDL_SCANCODE_S;
        const auto right = keyboard_layout == "arrows" ? SDL_SCANCODE_RIGHT : SDL_SCANCODE_D;
        if(code == forward) return key_forward;
        if(code == back) return key_back;
        if(code == left) return key_left;
        if(code == right) return key_right;
        switch(code) {
            case SDL_SCANCODE_SPACE: return key_jump; case SDL_SCANCODE_C: return key_slide;
            case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return key_crouch;
            case SDL_SCANCODE_R: return cat::combat::key_reload; case SDL_SCANCODE_Q: case SDL_SCANCODE_TAB: return cat::combat::key_switch;
            case SDL_SCANCODE_F: return cat::combat::key_revive;   // N8: hold F near a downed teammate
            default: return 0;
        }
    };
    auto clear_input = [&] {
        held_keys.fill(false);
        keys = taps = pad_keys = 0;
        shift_held = pad_sprint = false;
        axes = {};
        touch.reset();
    };
    auto open_pause = [&] { if(session && !menu.active()) { menu.show_pause(); clear_input(); } };
    auto hud_hit = [&](double x, double y) -> std::string { for(const auto& e : hud_elements) if((e.id == "pauseButton" || e.id == "fovToggle" || e.id == "mapbutton") && e.rect.contains(x, y)) return e.id; return {}; };
    auto hud_action = [&](const std::string& id) {
        if(id == "pauseButton") open_pause();
        else if(id == "fovToggle") { open_pause(); menu.show_settings(true); }
        else if(id == "mapbutton" && !coop.busy()) {   // legacy: loadMap(mapIndex + 1)
            const auto* m = find_map(map);
            run({{Command::Kind::start_game, kMaps[(static_cast<std::size_t>(m - kMaps) + 1) % 4].id}});
        }
    };
    while(running) {
        const auto frame_start = std::chrono::steady_clock::now();
        if(menu_selftest) selftest_step();
        if(coop_selftest) coop_step();
        loading_step();
        // ---- N8 network: relay I/O and its events, the server list
        coop.poll(net_now());
        for(const auto& e : coop.take_events()) {
            using EK = cat::net::CoopEvent::Kind;
            switch(e.kind) {
                case EK::ready: {
                    const auto* m = map_of_wire(e.map_id);
                    if(!m) { leave_server(); shell_status = "This server plays a map this build does not have."; shell_error = true; menu.show_multiplayer(); break; }
                    cat::log("info", "net", std::string(coop.mode() == cat::net::Coop::Mode::host ? "Hosting" : "Joined") + " server " + coop.room_code() + " on " + m->id + " as '" + gs.player_name + "'");
                    begin_loading(m->id);
                    break;
                }
                case EK::announce: if(session) { session->notice = e.text; session->notice_time = 2.6; } break;
                case EK::disconnected:
                    cat::log("warning", "net", e.text);
                    leave_server(); stop_game(); menu.show_multiplayer();
                    break;
                case EK::restart: restart_pending = true; break;
                case EK::remote_volley: if(session && !load.active) audio.gun(e.weapon); break;
            }
        }
        if(local_relay && relay_stop_at >= 0 && !coop.busy() && (local_relay->stats().peers == 0 || net_now() > relay_stop_at)) {
            local_relay->stop(); local_relay.reset(); relay_stop_at = -1; cat::log("info", "net", "Built-in relay stopped");
        }
        if(menu.screen() == Screen::connecting && !coop.busy()) {   // refused / failed before 'ready': back to the page it came from
            if(connect_from == Screen::maps) menu.show_host_maps(); else if(connect_from == Screen::servers) menu.show_servers(); else menu.show_multiplayer();
        }
        if(browse.valid() && browse.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const auto list = browse.get();
            mpv.listing = false; mpv.list_status = list.status; mpv.rooms.clear();
            for(const auto& r : list.rooms) { const auto* m = map_of_wire(r.map_id); mpv.rooms.push_back({r.id, r.title, r.host, m ? m->title : r.map_id, r.players, r.max_players, r.open, m != nullptr}); }
        }
        if(menu.screen() == Screen::servers && !coop.busy() && net_now() >= next_browse_ms) start_browse();   // the legacy 9 s refresh while the list is shown
        mpv.status = shell_status.empty() ? coop.status() : shell_status; mpv.status_error = shell_status.empty() ? coop.status_error() : shell_error;
        mpv.in_room = coop.in_room(); mpv.hosting = coop.mode() == cat::net::Coop::Mode::host;
        mpv.footer = std::string(mpv.hosting ? "CO-OP · HOSTING · " : "CO-OP · ") + std::to_string(coop.players_in_room()) + "/4";
        if(const bool want = menu.active() && menu.text_focused(); want != text_input_on) { if(want) SDL_StartTextInput(window); else SDL_StopTextInput(window); text_input_on = want; }
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_EVENT_QUIT: case SDL_EVENT_WINDOW_CLOSE_REQUESTED: running = false; break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: renderer.resize(); break;
                case SDL_EVENT_WINDOW_DISPLAY_CHANGED: display.display_changed(); break;
                case SDL_EVENT_AUDIO_DEVICE_ADDED: case SDL_EVENT_AUDIO_DEVICE_REMOVED:
                    if(!event.adevice.recording && (!device->ok() ||
                        (event.type == SDL_EVENT_AUDIO_DEVICE_REMOVED && device->owns_device(event.adevice.which)))) reconnect_audio = true;
                    break;
                case SDL_EVENT_WINDOW_RESTORED: case SDL_EVENT_WINDOW_SHOWN: renderer.resize(); audio.set_visible(true); break;
                case SDL_EVENT_WINDOW_MINIMIZED: case SDL_EVENT_WINDOW_HIDDEN: audio.set_visible(false); break;   // document.hidden pauses the soundtrack
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    focused = false; pointer.set_focused(false); clear_input();
                    if(!menu_selftest && !coop_selftest) open_pause();
                    break;
                case SDL_EVENT_WINDOW_FOCUS_GAINED: focused = true; pointer.set_focused(true); break;
                case SDL_EVENT_GAMEPAD_ADDED: if(SDL_Gamepad* g = SDL_OpenGamepad(event.gdevice.which)) { pads.push_back(g); cat::log("info", "input", std::string("Gamepad: ") + (SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "unnamed")); } break;
                case SDL_EVENT_GAMEPAD_REMOVED:
                    if(SDL_Gamepad* g = SDL_GetGamepadFromID(event.gdevice.which)) { pads.erase(std::remove(pads.begin(), pads.end(), g), pads.end()); SDL_CloseGamepad(g); }
                    axes = {}; pad_keys = 0; pad_sprint = false; break;
                case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: {
                    const bool down = event.type == SDL_EVENT_KEY_DOWN;
                    const auto code = event.key.scancode;
                    if(modes.note(cat::InputKind::kbm)) touch.reset();
                    if(down && !event.key.repeat && code == SDL_SCANCODE_F11) { set_fullscreen(!is_fullscreen()); break; }
                    if(menu.active()) {
                        if(!down) break;
                        if(menu.text_focused()) {   // a text field: Backspace erases, Space is typed (SDL_EVENT_TEXT_INPUT)
                            if(code == SDL_SCANCODE_BACKSPACE) { run(menu.handle({MenuEvent::Kind::key, cat::ui::NavKey::erase})); break; }
                            if(code == SDL_SCANCODE_SPACE) break;
                        }
                        if(auto k = nav_key(code, event.key.mod)) {
                            if(*k == cat::ui::NavKey::accept) audio.start();
                            run(menu.handle({MenuEvent::Kind::key, *k, 0, 0, 0, (event.key.mod & SDL_KMOD_SHIFT) != 0}));
                        }
                        break;
                    }
                    if(!session) break;
                    if(down && code == SDL_SCANCODE_ESCAPE) { open_pause(); break; }   // Esc releases the capture: pause menu (v0.18)
                    if(down && !event.key.repeat && code == SDL_SCANCODE_F5 && !coop.busy()) { begin_loading(map); break; }   // a server's map is fixed
                    if(down && !event.key.repeat && code == SDL_SCANCODE_V) { hud_action("fovToggle"); break; }   // V: settings (showFovPanel)
                    if(down && !event.key.repeat && code == SDL_SCANCODE_F9) { features.debug_view = (features.debug_view + 1) % 4; break; }
                    if(down && !event.key.repeat && code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_4) { if(!coop.busy()) run({{Command::Kind::start_game, kMaps[code - SDL_SCANCODE_1].id}}); break; }
                    if(!focused || event.key.repeat) break;
                    if(code <= SDL_SCANCODE_UNKNOWN || code >= SDL_SCANCODE_COUNT) break;
                    held_keys[static_cast<std::size_t>(code)] = down;
                    if(code == SDL_SCANCODE_LSHIFT || code == SDL_SCANCODE_RSHIFT) {
                        if(down && !shift_held) sprint.press();
                        shift_held = held_keys[SDL_SCANCODE_LSHIFT] || held_keys[SDL_SCANCODE_RSHIFT];
                        break;
                    }
                    const std::uint32_t bit = game_bit(code);
                    if(down) { keys |= bit; taps |= bit; }
                    else if(bit != 0) {
                        keys &= ~bit;
                        for(std::size_t i = 1; i < held_keys.size(); ++i)
                            if(held_keys[i] && game_bit(static_cast<SDL_Scancode>(i)) == bit) { keys |= bit; break; }
                    }
                    break;
                }
                case SDL_EVENT_TEXT_INPUT:
                    if(menu.active() && menu.text_focused() && event.text.text) run(menu.handle({MenuEvent::Kind::text, {}, 0, 0, 0, false, event.text.text}));
                    break;
                case SDL_EVENT_MOUSE_MOTION: {
                    if(event.motion.which == SDL_TOUCH_MOUSEID) break;
                    const auto [x, y] = pixels(event.motion.x, event.motion.y);
                    pointer_x = x; pointer_y = y;
                    if(menu.active()) { run(menu.handle({MenuEvent::Kind::pointer_move, {}, x, y})); break; }
                    if(session && focused && relative_on) {
                        modes.note(cat::InputKind::kbm);
                        session->arena.player().aim(event.motion.xrel * mouse_sensitivity, event.motion.yrel * mouse_sensitivity * mouse_y);
                    }   // legacy aim(): 0.003 rad per count
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP: {
                    if(event.button.which == SDL_TOUCH_MOUSEID) break;
                    const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    if(modes.note(cat::InputKind::kbm)) touch.reset();   // Android / DeX: a mouse switches AUTO to keyboard & mouse (v0.18)
                    const auto [x, y] = pixels(event.button.x, event.button.y);
                    if(menu.active()) {
                        if(event.button.button != SDL_BUTTON_LEFT) break;
                        if(down) audio.start();
                        run(menu.handle({down ? MenuEvent::Kind::pointer_down : MenuEvent::Kind::pointer_up, {}, x, y}));
                        break;
                    }
                    if(!session || !focused) break;
                    if(!relative_on) {   // not captured: HUD buttons, otherwise this click captures (attemptLock)
                        if(down && event.button.button == SDL_BUTTON_LEFT) { const auto id = hud_hit(x, y); if(!id.empty()) hud_action(id); else pointer.request(); }
                        break;
                    }
                    if(event.button.button != (mouse_fire == "right" ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT)) break;
                    if(down) { keys |= cat::combat::key_fire; taps |= cat::combat::key_fire; } else keys &= ~static_cast<std::uint32_t>(cat::combat::key_fire);
                    break;
                }
                case SDL_EVENT_MOUSE_WHEEL:
                    if(event.wheel.which == SDL_TOUCH_MOUSEID) break;
                    if(menu.active()) run(menu.handle({MenuEvent::Kind::wheel, {}, pointer_x, pointer_y, double(event.wheel.y)}));
                    break;
                case SDL_EVENT_FINGER_DOWN: case SDL_EVENT_FINGER_MOTION: case SDL_EVENT_FINGER_UP: case SDL_EVENT_FINGER_CANCELED: {
                    if(modes.note(cat::InputKind::touch)) { keys = taps = 0; }
                    int pw = 0, ph = 0; SDL_GetWindowSizeInPixels(window, &pw, &ph);
                    const double x = double(event.tfinger.x) * pw, y = double(event.tfinger.y) * ph;
                    const auto id = event.tfinger.fingerID;
                    const bool up = event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED;
                    if(menu.active()) {   // the first finger is the pointer
                        if(event.type == SDL_EVENT_FINGER_DOWN && !menu_finger) { menu_finger = id; audio.start(); run(menu.handle({MenuEvent::Kind::pointer_down, {}, x, y})); }
                        else if(menu_finger && *menu_finger == id) {
                            run(menu.handle({up ? MenuEvent::Kind::pointer_up : MenuEvent::Kind::pointer_move, {}, x, y}));
                            if(up) menu_finger.reset();
                        }
                        break;
                    }
                    if(!session || !modes.touch()) break;
                    if(event.type == SDL_EVENT_FINGER_DOWN) {
                        const double css = ui_scale(pw, ph, SDL_GetWindowDisplayScale(window));
                        const auto hit = touch.down(static_cast<std::int64_t>(id), x, y, hud_elements, css);
                        if(hit == "pause") open_pause(); else if(hit == "settings") hud_action("fovToggle"); else if(hit == "map") hud_action("mapbutton");
                    } else if(up) touch.up(static_cast<std::int64_t>(id));
                    else touch.move(static_cast<std::int64_t>(id), x, y, {gs.number("touchSensitivity"), gs.flag("touchInvert")});
                    break;
                }
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN: case SDL_EVENT_GAMEPAD_BUTTON_UP: {   // native addition (no legacy gamepad support)
                    const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                    if(modes.note(cat::InputKind::kbm)) touch.reset();
                    if(menu.active()) { if(down) if(auto k = pad_nav(event.gbutton.button)) { if(*k == cat::ui::NavKey::accept) audio.start(); run(menu.handle({MenuEvent::Kind::key, *k})); } break; }
                    if(!session) break;
                    std::uint32_t bit = 0;
                    switch(event.gbutton.button) {
                        case SDL_GAMEPAD_BUTTON_START: if(down) open_pause(); break;
                        case SDL_GAMEPAD_BUTTON_SOUTH: bit = cat::physics::key_jump; break;
                        case SDL_GAMEPAD_BUTTON_EAST: bit = cat::physics::key_crouch; break;
                        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: bit = cat::physics::key_slide; break;
                        case SDL_GAMEPAD_BUTTON_WEST: bit = cat::combat::key_reload; break;
                        case SDL_GAMEPAD_BUTTON_NORTH: case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: bit = cat::combat::key_switch; break;
                        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: bit = cat::combat::key_fire; break;
                        case SDL_GAMEPAD_BUTTON_LEFT_STICK: if(down) sprint.press(); pad_sprint = down; break;
                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: bit = cat::combat::key_revive; break;   // N8: hold to revive
                        default: break;
                    }
                    if(down) { pad_keys |= bit; taps |= bit; } else pad_keys &= ~bit;
                    break;
                }
                case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                    const double v = std::clamp(double(event.gaxis.value) / 32767.0, -1.0, 1.0);
                    switch(event.gaxis.axis) {
                        case SDL_GAMEPAD_AXIS_LEFTX: axes[0] = v; break; case SDL_GAMEPAD_AXIS_LEFTY: axes[1] = v; break;
                        case SDL_GAMEPAD_AXIS_RIGHTX: axes[2] = v; break; case SDL_GAMEPAD_AXIS_RIGHTY: axes[3] = v; break;
                        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: axes[4] = v; break; case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: axes[5] = v; break;
                        default: break;
                    }
                    break;
                }
                default: break;
            }
        }
        if(!running) break;
        if(reconnect_audio) { reconnect_audio = false; open_audio(); }
        if(wipe_pending || restart_pending) { wipe_pending = restart_pending = false; if(coop.in_room() && session) restart_round(); }
        for(int n = touch.take_sprint_presses(); n > 0; --n) sprint.press();
        // pointer capture (v0.18): only in play, never with a menu open, in touch mode or without focus
        pointer.set_game_active(session.has_value() && !load.active);
        pointer.set_menu_open(menu.active());
        pointer.set_touch_mode(modes.touch());
        relative_on = SDL_GetWindowRelativeMouseMode(window);
        if(!pointer.wanted() && !relative_on) pointer.platform_result(false);
        if(pointer.wanted() != relative_on) {
            const bool want = pointer.wanted();
            const bool ok = SDL_SetWindowRelativeMouseMode(window, want);
            relative_on = SDL_GetWindowRelativeMouseMode(window);
            pointer.platform_result(want && ok && relative_on);
            if(!ok) {
                cat::log("warning", "input", std::string("Mouse capture: ") + SDL_GetError());
                pointer.release();
            }
            if(!want) keys &= ~static_cast<std::uint32_t>(cat::combat::key_fire);
        }
        audio.set_in_game(session.has_value() && !load.active);
        // ---- simulation (stopped while any menu is open: the legacy pause)
        simulation.advance(timer.lap(), [&](double) {
            // N8: in a server the arena keeps running behind the menus (the other players are still playing); the menu keeps the input
            const bool menu_input = menu.active() || !focused;
            if(!session || (menu.blocks_game() && !coop.in_room()) || load.active) { taps = 0; touch.end_tick(); return; }
            int pw = 0, ph = 0;
            const double aspect = SDL_GetWindowSizeInPixels(window, &pw, &ph) && pw > 0 && ph > 0 ? double(pw) / double(ph) : 16.0 / 9.0;
            std::uint32_t k = menu_input ? 0U : keys | taps | pad_keys | touch.keys();
            if(axes[5] > .35 && !menu_input) k |= cat::combat::key_fire;   // right trigger
            if(!menu_input && sprint.requested(shift_held || touch.sprint_held() || pad_sprint)) k |= cat::physics::key_sprint;
            double sx = touch.stick_x(), sy = touch.stick_y();
            if(sx == 0 && sy == 0 && std::hypot(axes[0], axes[1]) > .2) { sx = axes[0]; sy = axes[1]; }   // left stick: direction as the touch stick
            double lx = touch.look_dx(), ly = touch.look_dy();
            for(int i : {2, 3}) {   // right stick: up to 2.6 rad/s at full deflection (0.003 rad per count)
                const double v = std::fabs(axes[static_cast<std::size_t>(i)]) > .15 ? axes[static_cast<std::size_t>(i)] * std::fabs(axes[static_cast<std::size_t>(i)]) * 2.6 / 120 / .003 : 0;
                (i == 2 ? lx : ly) += v;
            }
            if(menu_input) lx = ly = sx = sy = 0;
            session->step({k, lx, ly, sx, sy}, aspect);
            coop.after_step();
            if(coop.mode() == cat::net::Coop::Mode::host && session->arena.events().wipe) wipe_pending = true;
            const auto& pl = session->arena.player().state();
            audio.step(session->arena.events(), session->arena.player().body_events(), {pl.x, pl.y, pl.a}, session->arena.now());
            taps = 0; touch.end_tick();
        });
        // ---- picture: arena (or the maps page preview) + HUD + menus in one UI pass
        int pw = 0, ph = 0;
        const double cpu_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame_start).count();
        if(SDL_GetWindowSizeInPixels(window, &pw, &ph) && pw > 0 && ph > 0) {
            const std::pair<std::uint32_t, std::uint32_t> size{static_cast<std::uint32_t>(pw), static_cast<std::uint32_t>(ph)};
            const double W = pw, H = ph, density = SDL_GetWindowPixelDensity(window), scale = ui_scale(W, H, SDL_GetWindowDisplayScale(window));
            cat::ui::Rect safe{0, 0, W, H};
            SDL_Rect sa{};
            if(SDL_GetWindowSafeArea(window, &sa) && sa.w > 0 && sa.h > 0) safe = {sa.x * density, sa.y * density, sa.w * density, sa.h * density};
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
            cat::ui::Canvas canvas(&kit.atlas, W, H);
            cat::Overlay o;
            std::optional<cat::FrameView> view;
            const bool in_game = session.has_value() && (!load.active || load.phase >= 6);
            if(in_game) {
                view.emplace(cat::FrameView{player_camera(session->arena.player(), size), session->arena.player().now(), features});
                // N8: the other players as Marine Cats (netPlayers), ids stable per relay id
                session->enemy_options.guests.clear();
                for(const auto& p : coop.players()) {
                    if(!p.placed) continue;
                    cat::fx::Guest g;
                    g.id = static_cast<std::uint32_t>(std::hash<std::string>{}(p.id) | 1U);
                    g.x = p.x; g.y = p.y; g.z = p.z; g.a = p.a; g.vx = p.vx; g.vy = p.vy; g.step = p.step; g.color = p.color; g.shoot_until = p.shoot_until; g.downed = p.downed;
                    session->enemy_options.guests.push_back(g);
                }
                o = session->build(size); session->light(*view);
                cat::ui::HudView hv; hv.settings = &gs; hv.scale = scale; hv.safe = safe; hv.touch = modes.touch(); hv.captured = relative_on; hv.show_mouse_status = !modes.touch();
                hv.perf = perf_lines(gs, perf.summary(), renderer.stats(), renderer.timestamps_supported());
                hv.pressed = touch.pressed(); hv.knob_x = touch.stick_x(); hv.knob_y = touch.stick_y();
                hv.editing = menu.screen() == Screen::hud_editor; hv.selected = menu.hud_selected();
                if(!load.active) hud_elements = cat::ui::build_hud(canvas, hud_info(*session), hv);
                if(!load.active && coop.in_room()) {   // N8: name tags, revive line, room badge
                    cat::ui::CoopHud ch;
                    ch.badge = coop.badge(); ch.revive = coop.revive_status();
                    ch.revive_alert = session->arena.player().state().downed || ch.revive.rfind("SQUAD", 0) == 0;
                    for(const auto& p : coop.players()) {
                        if(!p.placed) continue;
                        auto t = project_tag(session->frame.camera, *collision, p.x, p.y, p.z, p.downed, W, H);
                        if(!t) continue;
                        t->name = p.name; t->hp = p.hp; t->host = p.is_host;
                        ch.tags.push_back(*t);
                    }
                    coop_tags_drawn = 0;
                    for(const auto& t : ch.tags) coop_tags_drawn += t.x >= 0 && t.x <= W && t.y >= 0 && t.y <= H;
                    cat::ui::build_coop_hud(canvas, ch, hv);
                }
            } else if(menu.screen() == Screen::maps && scene && scene_map == menu.selected_map()) {   // the live view of the chosen map
                view.emplace(cat::FrameView{camera_for(preview_pose(scene_map), size), seconds, features});
                o.world = false;
            }
            if(menu.active()) {
                cat::ui::MenuFrame f; f.time = seconds; f.scale = scale; f.safe = safe; f.in_game = session.has_value(); f.current_map = map; f.touch = modes.touch();
                f.pointer_x = pointer_x; f.pointer_y = pointer_y; f.hud = hud_elements; f.background = kit.background; f.mp = &mpv;
#ifdef __ANDROID__
                f.platform = "ANDROID";
#else
                f.platform = "DESKTOP";
#endif
                f.status = device->ok() ? "" : "SOUND: NO AUDIO DEVICE";
                if(menu.screen() == Screen::settings) f.display_status = display.status();
                menu.build(canvas, f);
            }
            attach_ui(o, canvas);
            if(view) renderer.draw(*view, o); else renderer.draw_ui(o);
            ++rendered;
        }
        perf.frame(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_start).count(), cpu_ms);
        if(coop_selftest && rendered > 9000) { ct.failure = "timed out at stage " + std::to_string(ct.stage); running = false; }
        if(menu_selftest && rendered > 6000) { mt.failure = "timed out at stage " + std::to_string(mt.stage); running = false; }
        if(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) SDL_Delay(20);
        else if(frame_cap > 0) std::this_thread::sleep_until(frame_start + std::chrono::nanoseconds(1'000'000'000 / frame_cap));
    }
    if(relative_on) SDL_SetWindowRelativeMouseMode(window, false);
    if(text_input_on) SDL_StopTextInput(window);
    detach_coop(); leave_server();
    if(local_relay) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); local_relay->stop(); local_relay.reset(); }
    if(browse.valid()) std::thread([f = std::move(browse)]() mutable { f.wait(); }).detach();   // a slow server list must not hold up the exit
    save_settings();
    for(SDL_Gamepad* g : pads) SDL_CloseGamepad(g);
    const auto& counts = audio.counts();
    cat::log("info", "audio", "Sounds: " + std::to_string(counts.guns) + " gun, " + std::to_string(counts.explosions) + " explosion, " + std::to_string(counts.bodies) + " body, " + std::to_string(counts.voices) + " voices (" +
                                  std::to_string(counts.voices_suppressed) + " suppressed by the legacy limits), " + std::to_string(counts.beeps) + " beeps; " + std::to_string(mixer.rendered_frames()) + " frames mixed");
    int result = 0;
    if(menu_selftest) {
        std::ifstream saved(settings_path);
        const std::string text((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
        const bool saved_ok = text.find("fovControl=110") != std::string::npos && text.find("masterVolume=60") != std::string::npos;
        const bool ok = mt.failure.empty() && mt.home_ok && mt.preview_ok && mt.loaded_ok && mt.playing_ok && mt.fired_ok && mt.paused_ok && mt.settings_ok && mt.returned_ok && mt.quit_ok && saved_ok;
        auto yn = [](bool b) { return b ? "yes" : "no"; };
        cat::log(ok ? "info" : "error", "app", std::string("Menu self-test ") + (ok ? "PASS" : "FAIL") + ": home=" + yn(mt.home_ok) + " mapPreview=" + yn(mt.preview_ok) + " loadingReady=" + yn(mt.loaded_ok) +
                     " enterArena=" + yn(mt.playing_ok) + " fireSound=" + yn(mt.fired_ok) + " escPause=" + yn(mt.paused_ok) + " fov110=" + yn(mt.fov_ok) + " master60=" + yn(mt.volume_ok) +
                     " returnToMenu=" + yn(mt.returned_ok) + " exitDialog=" + yn(mt.quit_ok) + " settingsSaved=" + yn(saved_ok) + " captureSupported=" + yn(pointer.failures() == 0) +
                     " audioDevice=" + (device->ok() ? device->status() : std::string("none (") + device->status() + ")") + " frames=" + std::to_string(rendered) + " ticks=" + std::to_string(simulation.ticks()) +
                     (mt.failure.empty() ? "" : " failure=\"" + mt.failure + "\""));
        result = ok ? 0 : 3;
        std::error_code ec; if(!game_settings_file) std::filesystem::remove(settings_path, ec);
    }
    if(coop_selftest) {
        const bool ok = ct.failure.empty() && ct.menus_ok && ct.typed_ok && ct.hosting_ok && ct.listed_ok && ct.joined_ok && ct.world_ok && ct.marines_ok && ct.tags_ok && ct.damage_ok && ct.revive_ok && ct.menu_runs_ok && ct.left_ok && ct.quit_ok;
        auto yn = [](bool b) { return b ? "yes" : "no"; };
        cat::log(ok ? "info" : "error", "app", std::string("Co-op self-test ") + (ok ? "PASS" : "FAIL") + ": hostMenus=" + yn(ct.menus_ok) + " typedName=" + yn(ct.typed_ok) + " hostOnThisPc=" + yn(ct.hosting_ok) +
                     " serverList=" + yn(ct.listed_ok) + " threeGuests=" + yn(ct.joined_ok) + " snapshots=" + yn(ct.world_ok) + " marineCats=" + std::to_string(ct.marines) + " nameTags=" + std::to_string(ct.tags) +
                     " downed=" + yn(ct.damage_ok) + " revive=" + yn(ct.revive_ok) + " menuKeepsRunning=" + yn(ct.menu_runs_ok) + " leave=" + yn(ct.left_ok) + " exit=" + yn(ct.quit_ok) +
                     " frames=" + std::to_string(rendered) + " ticks=" + std::to_string(simulation.ticks()) + (ct.failure.empty() ? "" : " failure=\"" + ct.failure + "\""));
        result = ok ? 0 : 3;
        std::error_code ec; if(!game_settings_file) std::filesystem::remove(settings_path, ec);
    }
    if(report) {
        std::ofstream r(*report);
        const auto dev = renderer.report();
        r << "{\"device\": \"" << json_escape(dev.device) << "\", \"api\": \"" << dev.api << "\", \"presentMode\": \"" << dev.present_mode << "\", \"menus\": true, \"framesSubmitted\": " << rendered
          << ", \"ticks\": " << simulation.ticks() << ", \"settingsSaves\": " << saves << ", \"audioFramesMixed\": " << mixer.rendered_frames() << ", \"gunSounds\": " << counts.guns
          << ", \"validationWarnings\": " << renderer.validation_warnings() << ", \"validationErrors\": " << renderer.validation_errors() << "}\n";
    }
    device.reset();   // stop the audio thread before the mixer goes
    cat::log("info", "app", "Clean shutdown; frames=" + std::to_string(rendered) + " validationWarnings=" + std::to_string(renderer.validation_warnings()) + " validationErrors=" + std::to_string(renderer.validation_errors()));
    return renderer.validation_errors() ? 2 : result;
}
