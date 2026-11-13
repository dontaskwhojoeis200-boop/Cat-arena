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
    else if(menu_selftest) { settings_path = std::filesystem::temp_directory_path() / "cat_arena_menu_selftest_settings.txt"; std::error_code ec; std::filesystem::remove(settings_path, ec); }
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
    // ---- sound: mixer on the device thread, legacy sample bank and the ASHEN DRIVE theme
    cat::audio::Mixer mixer(48000);
    std::optional<cat::audio::Device> device;
    device.emplace(mixer);
    cat::log(device->ok() ? "info" : "warning", "audio", device->ok() ? "Audio: " + device->status() : "Silent: " + device->status());
    auto bank = std::make_shared<const cat::audio::SampleBank>(cat::audio::SampleBank::load(n7 / "audio" / "sfx", mixer.rate()));
    cat::audio::GameAudio audio(mixer, bank);
    mixer.set_music(cat::audio::open_mp3(cat::audio::read_file(n7 / "audio" / "main-theme.mp3")));
    cat::log("info", "audio", "Sound bank and soundtrack from " + cat::path_utf8(n7 / "audio") + " in " + std::to_string(static_cast<int>(std::chrono::duration<double, std::milli>(ui_clock.lap()).count())) + " ms");
    // ---- input state
    cat::InputModeTracker modes; cat::PointerCapture pointer; cat::TouchControls touch; cat::SprintLatch sprint; cat::PerfMeter perf;
    std::vector<SDL_Gamepad*> pads;
    std::array<double, 6> axes{};   // left x / y, right x / y, left / right trigger (-1..1, 0..1)
    std::uint32_t keys = 0, taps = 0, pad_keys = 0;
    bool shift_held = false, pad_sprint = false, relative_on = false, fullscreen_launch_done = false;
    std::optional<SDL_FingerID> menu_finger;
    std::vector<cat::ui::HudElement> hud_elements;
    double pointer_x = -1, pointer_y = -1;
    // ---- settings applied to the renderer, the session and the mixer
    bool vsync_now = settings.vsync; int frame_cap = settings.frame_cap; int applied = 0;
    auto apply_settings = [&] {
        if(!fov_given) fov = gs.number("fovControl");
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
        player = nullptr; session.reset();
        load_render_scene(id);
        renderer.load_scene(*scene);
        attach_light_volume(renderer);
        scene_map = id;
    };
    struct { bool active{}; std::string map; int phase{}; double ms{}; std::string error; } load;
    auto loading_state = [&] {
        cat::ui::LoadingState s; s.map_title = find_map(load.map)->title; s.phase = load.phase; s.ready = load.active && load.phase >= 7; s.error = load.error; s.millis = load.ms;
        char b[96]; std::snprintf(b, sizeof b, "NATIVE · %.0f MS", load.ms); s.info = b;
        return s;
    };
    bool overlay_ready = false;
    auto begin_loading = [&](const std::string& id) {
        player = nullptr; session.reset();
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
                case 5: spawn_index = 0; start_session(); session->show_hud = false; apply_settings(); break;                          // PLACE ENEMIES + PICKUPS
                case 6: break;                                                                                                        // WARM UP GRAPHICS (this frame draws the arena)
                default: break;
            }
            load.ms += std::chrono::duration<double, std::milli>(t.lap()).count();
            ++load.phase;
        } catch(const std::exception& e) {
            load.error = e.what();
            cat::log("error", "scene", std::string("Loading ") + load.map + " failed: " + e.what());
            player = nullptr; session.reset();
        }
        menu.update_loading(loading_state());
        if(load.phase >= 7) cat::log("info", "app", "Loaded " + load.map + " in " + std::to_string(static_cast<int>(load.ms)) + " ms: waiting for ENTER THE ARENA");
    };
    auto stop_game = [&] { player = nullptr; session.reset(); load = {}; audio.set_in_game(false); keys = taps = 0; touch.reset(); };
    auto set_fullscreen = [&](bool on) { SDL_SetWindowFullscreen(window, on); };
    auto is_fullscreen = [&] { return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0; };
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
                    if(gs.flag("autoFullscreen") && !fullscreen_launch_done && !is_fullscreen() && !menu_selftest) { set_fullscreen(true); fullscreen_launch_done = true; }
                    timer.lap();
                    break;
                case Command::Kind::resume: pointer.set_menu_open(false); pointer.request(); timer.lap(); break;
                case Command::Kind::return_to_menu: stop_game(); menu.show_home(); break;
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
    // ---- the loop
    const auto t_start = std::chrono::steady_clock::now();
    auto pixels = [&](double wx, double wy) { const double d = SDL_GetWindowPixelDensity(window); return std::pair{wx * d, wy * d}; };
    auto game_bit = [](SDL_Scancode code) -> std::uint32_t {   // the legacy key codes, by physical key
        using namespace cat::physics;
        switch(code) {
            case SDL_SCANCODE_W: return key_forward; case SDL_SCANCODE_S: return key_back; case SDL_SCANCODE_A: return key_left; case SDL_SCANCODE_D: return key_right;
            case SDL_SCANCODE_SPACE: return key_jump; case SDL_SCANCODE_C: return key_slide;
            case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return key_crouch;
            case SDL_SCANCODE_R: return cat::combat::key_reload; case SDL_SCANCODE_Q: case SDL_SCANCODE_TAB: return cat::combat::key_switch;
            default: return 0;
        }
    };
    auto open_pause = [&] { if(session && !menu.active()) { menu.show_pause(); keys = taps = pad_keys = 0; touch.reset(); } };
    auto hud_hit = [&](double x, double y) -> std::string { for(const auto& e : hud_elements) if((e.id == "pauseButton" || e.id == "fovToggle" || e.id == "mapbutton") && e.rect.contains(x, y)) return e.id; return {}; };
    auto hud_action = [&](const std::string& id) {
        if(id == "pauseButton") open_pause();
        else if(id == "fovToggle") { open_pause(); menu.show_settings(true); }
        else if(id == "mapbutton") {   // legacy: loadMap(mapIndex + 1)
            const auto* m = find_map(map);
            run({{Command::Kind::start_game, kMaps[(static_cast<std::size_t>(m - kMaps) + 1) % 4].id}});
        }
    };
    while(running) {
        const auto frame_start = std::chrono::steady_clock::now();
        if(menu_selftest) selftest_step();
        loading_step();
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_EVENT_QUIT: case SDL_EVENT_WINDOW_CLOSE_REQUESTED: running = false; break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: renderer.resize(); break;
                case SDL_EVENT_WINDOW_RESTORED: case SDL_EVENT_WINDOW_SHOWN: renderer.resize(); audio.set_visible(true); break;
                case SDL_EVENT_WINDOW_MINIMIZED: case SDL_EVENT_WINDOW_HIDDEN: audio.set_visible(false); break;   // document.hidden pauses the soundtrack
                case SDL_EVENT_WINDOW_FOCUS_LOST: pointer.set_focused(false); if(!menu_selftest) open_pause(); keys = taps = 0; shift_held = false; break;   // blur releases the capture
                case SDL_EVENT_WINDOW_FOCUS_GAINED: pointer.set_focused(true); break;
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
                        if(auto k = nav_key(code, event.key.mod)) {
                            if(*k == cat::ui::NavKey::accept) audio.start();
                            run(menu.handle({MenuEvent::Kind::key, *k, 0, 0, 0, (event.key.mod & SDL_KMOD_SHIFT) != 0}));
                        }
                        break;
                    }
                    if(!session) break;
                    if(down && code == SDL_SCANCODE_ESCAPE) { open_pause(); break; }   // Esc releases the capture: pause menu (v0.18)
                    if(down && !event.key.repeat && code == SDL_SCANCODE_F5) { begin_loading(map); break; }
                    if(down && !event.key.repeat && code == SDL_SCANCODE_V) { hud_action("fovToggle"); break; }   // V: settings (showFovPanel)
                    if(down && !event.key.repeat && code == SDL_SCANCODE_F9) { features.debug_view = (features.debug_view + 1) % 4; break; }
                    if(down && !event.key.repeat && code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_4) { run({{Command::Kind::start_game, kMaps[code - SDL_SCANCODE_1].id}}); break; }
                    if(code == SDL_SCANCODE_LSHIFT || code == SDL_SCANCODE_RSHIFT) { if(down && !event.key.repeat) sprint.press(); shift_held = down; break; }
                    if(event.key.repeat) break;
                    const std::uint32_t bit = game_bit(code);
                    if(down) { keys |= bit; taps |= bit; } else keys &= ~bit;
                    break;
                }
                case SDL_EVENT_MOUSE_MOTION: {
                    if(event.motion.which == SDL_TOUCH_MOUSEID) break;
                    const auto [x, y] = pixels(event.motion.x, event.motion.y);
                    pointer_x = x; pointer_y = y;
                    if(menu.active()) { run(menu.handle({MenuEvent::Kind::pointer_move, {}, x, y})); break; }
                    if(session && relative_on) { modes.note(cat::InputKind::kbm); session->arena.player().aim(double(event.motion.xrel), double(event.motion.yrel)); }   // legacy aim(): 0.003 rad per count
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
                    if(!session || event.button.button != SDL_BUTTON_LEFT) break;
                    if(!relative_on) {   // not captured: HUD buttons, otherwise this click captures (attemptLock)
                        if(down) { const auto id = hud_hit(x, y); if(!id.empty()) hud_action(id); else pointer.request(); }
                        break;
                    }
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
        for(int n = touch.take_sprint_presses(); n > 0; --n) sprint.press();
        // pointer capture (v0.18): only in play, never with a menu open, in touch mode or without focus
        pointer.set_game_active(session.has_value() && !load.active);
        pointer.set_menu_open(menu.active());
        pointer.set_touch_mode(modes.touch());
        if(pointer.wanted() != relative_on) {
            const bool want = pointer.wanted();
            const bool ok = SDL_SetWindowRelativeMouseMode(window, want);
            relative_on = want && ok;
            if(want) pointer.platform_result(relative_on);
            if(!want) keys &= ~static_cast<std::uint32_t>(cat::combat::key_fire);
        }
        audio.set_in_game(session.has_value() && !load.active);
        // ---- simulation (stopped while any menu is open: the legacy pause)
        simulation.advance(timer.lap(), [&](double) {
            if(!session || menu.blocks_game() || load.active) { taps = 0; touch.end_tick(); return; }
            int pw = 0, ph = 0;
            const double aspect = SDL_GetWindowSizeInPixels(window, &pw, &ph) && pw > 0 && ph > 0 ? double(pw) / double(ph) : 16.0 / 9.0;
            std::uint32_t k = keys | taps | pad_keys | touch.keys();
            if(axes[5] > .35) k |= cat::combat::key_fire;   // right trigger
            if(sprint.requested(shift_held || touch.sprint_held() || pad_sprint)) k |= cat::physics::key_sprint;
            double sx = touch.stick_x(), sy = touch.stick_y();
            if(sx == 0 && sy == 0 && std::hypot(axes[0], axes[1]) > .2) { sx = axes[0]; sy = axes[1]; }   // left stick: direction as the touch stick
            double lx = touch.look_dx(), ly = touch.look_dy();
            for(int i : {2, 3}) {   // right stick: up to 2.6 rad/s at full deflection (0.003 rad per count)
                const double v = std::fabs(axes[static_cast<std::size_t>(i)]) > .15 ? axes[static_cast<std::size_t>(i)] * std::fabs(axes[static_cast<std::size_t>(i)]) * 2.6 / 120 / .003 : 0;
                (i == 2 ? lx : ly) += v;
            }
            session->step({k, lx, ly, sx, sy}, aspect);
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
                o = session->build(size); session->light(*view);
                cat::ui::HudView hv; hv.settings = &gs; hv.scale = scale; hv.safe = safe; hv.touch = modes.touch(); hv.captured = relative_on; hv.show_mouse_status = !modes.touch();
                hv.perf = perf_lines(gs, perf.summary(), renderer.stats(), renderer.timestamps_supported());
                hv.pressed = touch.pressed(); hv.knob_x = touch.stick_x(); hv.knob_y = touch.stick_y();
                hv.editing = menu.screen() == Screen::hud_editor; hv.selected = menu.hud_selected();
                if(!load.active) hud_elements = cat::ui::build_hud(canvas, hud_info(*session), hv);
            } else if(menu.screen() == Screen::maps && scene && scene_map == menu.selected_map()) {   // the live view of the chosen map
                view.emplace(cat::FrameView{camera_for(preview_pose(scene_map), size), seconds, features});
                o.world = false;
            }
            if(menu.active()) {
                cat::ui::MenuFrame f; f.time = seconds; f.scale = scale; f.safe = safe; f.in_game = session.has_value(); f.current_map = map; f.touch = modes.touch();
                f.pointer_x = pointer_x; f.pointer_y = pointer_y; f.hud = hud_elements; f.background = kit.background;
#ifdef __ANDROID__
                f.platform = "ANDROID";
#else
                f.platform = "DESKTOP";
#endif
                f.status = device->ok() ? "" : "SOUND: NO AUDIO DEVICE";
                menu.build(canvas, f);
            }
            attach_ui(o, canvas);
            if(view) renderer.draw(*view, o); else renderer.draw_ui(o);
            ++rendered;
        }
        perf.frame(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_start).count(), cpu_ms);
        if(menu_selftest && rendered > 6000) { mt.failure = "timed out at stage " + std::to_string(mt.stage); running = false; }
        if(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) SDL_Delay(20);
        else if(frame_cap > 0) std::this_thread::sleep_until(frame_start + std::chrono::nanoseconds(1'000'000'000 / frame_cap));
    }
    if(relative_on) SDL_SetWindowRelativeMouseMode(window, false);
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
