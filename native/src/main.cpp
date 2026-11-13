// Cat Arena native shell. N0: SDL3 window + Vulkan + 120 Hz clock. N2: renders a baked map.
//   cat_arena --headless-test                      core self-test, no SDL/GPU
//   cat_arena [--map helios-crucible]              interactive window, first-person traversal (N3):
//                                                  click to capture the mouse, WASD move, Shift sprint,
//                                                  Space jump, Ctrl crouch, C slide, Esc release/quit
//   cat_arena --frames 400 --input-selftest        window mode: pushes synthetic SDL keyboard/mouse events through the
//                                                  normal event path (capture click, W, mouse turn, Shift, Space, C, Ctrl)
//                                                  and fails (exit 3) unless the player moved, turned, jumped and slid
//                                                  (N6: without enemies unless --enemies is given: a frag would reroll the weapon)
// N4 map selection (legacy map ids; default helios-crucible):
//   cat_arena --list-maps                          print the four maps (id, wire id, title) and exit
//   cat_arena --map bastion-keep|reactor-bastion|q3-gallery
//   in the window: keys 1-4 switch map (HELIOS CRUCIBLE, BASTION KEEP, REACTOR BASTION, THE ARSENAL)
//   cat_arena --frames 200 --map-cycle-test        window mode: switch through all four maps while rendering
//   cat_arena --fly                                N2 debug fly camera instead of the player (no collision)
//   cat_arena --spawn 3                            start at baked spawn point 3 (default 0)
//   cat_arena --offscreen 1280x720 --walk "w+shift:240,w+shift+jump:3,w@4:200" --capture-every 60 --capture-dir out/
//                                                  headless traversal: segments keys[@mouseDx[/mouseDy]]:ticks,
//                                                  keys from w s a d jump crouch sprint slide (or "-"), 120 ticks = 1 s
//   cat_arena --offscreen 1280x720 --capture a.png --camera x,y,z,yaw,pitch
//   cat_arena --offscreen 1280x720 --capture-set poses.txt --capture-dir out/   (one scene load)
// N5 weapons (on by default whenever a player exists: window mode and --walk):
//   in the window: left mouse fires, R reloads, Q or Tab swaps the two weapons; a frag replaces the
//   weapon in hand with a random other one (the legacy rule). Practice targets stand in for enemies (N6).
//   --weapons 4,2                  start with these two weapons (indices 0..15, see --list-weapons); default: random pair
//   --seed 7                       seed of the gameplay random stream (spread, loadout rolls); default: 1 offscreen,
//                                  a different seed every run (and every map change) in the window
//   --no-targets                   no practice targets
//   --no-overlay                   no weapon model, effects or HUD (the N4 picture); weapons still simulate
//   --no-hud                       weapon model and effects without the HUD (for comparisons with legacy frames)
//   --features -dlights            N5.1: turn the dynamic lights of flashes, projectiles and explosions off
//   --player-pose x,y,z,yaw,pitch  start the player here instead of at a spawn point
//   --list-weapons                 print the 16 weapons and exit
//   --offscreen WxH --combat-fixture native/tests/fixtures/n5/helios_combat.bin --scenario rocket_at_feet_moving
//             --capture-ticks 59,62,70 --capture-dir out/      replay one recorded legacy tape (start state, targets, seed)
//                                                              and write <scenario>_<tick>.png after those ticks
//   --walk also accepts the keys fire, reload (r) and swap (q):  --walk "fire:120,-:40,swap:2,fire@3:60"
// N6 enemies (default whenever a player exists): waves of the 20 creature types, as the legacy game.
//   --enemies waves|practice|none  waves (default), the N5 practice targets, or an empty map
//   --wave 5                       start at this wave (wave 5 brings all three flyer types at once)
//   --shadows 0|1|3                creature shadows: none, blobs (default, the legacy default) or projected bodies
//   --no-light-grid                creatures are lit by the fixed light instead of the baked light volume
//   --third-person                 debug view from behind the player: draws the local Marine Cat, hides the
//                                  first-person weapon (first person never draws the local Marine Cat)
//   --god                          the player takes no damage (for looking at enemies)
//   --offscreen WxH --enemy-fixture native/tests/fixtures/n6/helios-crucible_enemies.bin --scenario fight_spin
//             --capture-ticks 600,1200 --capture-dir out/     replay one recorded legacy enemy tape and write frames
//   --offscreen WxH --enemy-lineup lineup.txt --lineup-shot windup --capture out.png
//                                  one staged scene written by tools/n6/legacy_enemies.cjs --lineup-dir (all 20 types,
//                                  attack wind-up, final five, bodies, Marine Cats); every random draw is 0.5 as there
// N2.1 Vulkan path control (default: highest path the device supports, Vulkan 1.2 minimum):
//   --resize-test                  window mode with --frames N: resize the window halfway (swapchain recreation)
//   --vk-info                      print detected capabilities and the selected path as JSON, then exit
//   --vk-baseline                  force the mandatory Vulkan 1.2 path (no optional feature)
//   --vk-max-api 1.2|1.3|1.4       cap the API version used
//   --vk-no-extensions             use core-promoted features only
//   --vk-disable a,b               disable features: dynamic_rendering, synchronization2,
//                                  extended_dynamic_state, push_descriptor, host_image_copy
// N7 menus, settings, HUD and sound (assets/n7):
//   cat_arena                      opens the main menu (PLAY / MULTIPLAYER / SETTINGS / EXIT) in the legacy
//                                  routing: map selection with the live map view, loading screen, ENTER THE ARENA,
//                                  Esc = pause menu (resume, settings, HUD editor, another map, main menu).
//                                  Keyboard, mouse, touch and gamepad drive every page. Settings are saved in the
//                                  user folder (SDL_GetPrefPath "CatArena/FeralClaw/settings.txt").
//   cat_arena --map reactor-bastion  goes straight to the loading screen of that map
//   --game-settings <file>         read / write the settings here instead of the user folder
//   --menu-selftest                window mode: drives the menus with synthetic key events (home -> maps -> loading ->
//                                  game -> pause -> settings: FOV and master volume -> main menu -> exit) and
//                                  fails (exit 3) unless every step took effect
//   --offscreen 1280x720 --ui-capture <dir> [--touch]
//                                  writes every menu page, the settings tabs and the in-game HUD as PNG files
#include "cat/app_controls.hpp"
#include "cat/audio.hpp"
#include "cat/audio_device.hpp"
#include "cat/combat.hpp"
#include "cat/enemy_fx.hpp"
#include "cat/game_settings.hpp"
#include "cat/input_modes.hpp"
#include "cat/menu.hpp"
#include "cat/platform_info.hpp"
#include "cat/ui.hpp"
#include "cat/ui_hud.hpp"
#include "cat/core.hpp"
#include "cat/hud.hpp"
#include "cat/physics.hpp"
#include "cat/weapon_fx.hpp"
#include "cat/renderer.hpp"
#include "cat/scene.hpp"
#include "cat/coop.hpp"
#include "cat/relay.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>
#ifndef CAT_DEFAULT_ASSET_ROOT
#define CAT_DEFAULT_ASSET_ROOT ""
#endif
namespace {
std::optional<cat::Key> key(SDL_Scancode code) {
    switch(code) {
        case SDL_SCANCODE_W: return cat::Key::w; case SDL_SCANCODE_S: return cat::Key::s;
        case SDL_SCANCODE_A: return cat::Key::a; case SDL_SCANCODE_D: return cat::Key::d;
        case SDL_SCANCODE_SPACE: return cat::Key::space; case SDL_SCANCODE_ESCAPE: return cat::Key::escape;
        case SDL_SCANCODE_LCTRL: return cat::Key::left_ctrl; case SDL_SCANCODE_LSHIFT: return cat::Key::left_shift;
        default: return {};
    }
}
int number(std::string_view s) {
    int n = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
    if(ec != std::errc{} || ptr != s.data() + s.size() || n < 0) throw std::runtime_error("Invalid nonnegative integer argument");
    return n;
}
double real(const std::string& s) {
    std::size_t used = 0;
    const double v = std::stod(s, &used);
    if(used != s.size() || !std::isfinite(v)) throw std::runtime_error("Invalid number: " + s);
    return v;
}
std::vector<double> numbers(std::string_view list, char sep) {
    std::vector<double> out;
    std::string item;
    std::istringstream in{std::string(list)};
    while(std::getline(in, item, sep)) out.push_back(real(item));
    return out;
}
struct Pose { std::string name; double x{}, y{}, z{}, yaw{}, pitch{}; };
Pose parse_pose(std::string_view text) {
    auto v = numbers(text, ',');
    if(v.size() != 5) throw std::runtime_error("--camera expects x,y,feetZ,yaw,pitch");
    return {"camera", v[0], v[1], v[2], v[3], v[4]};
}
std::vector<Pose> read_poses(const std::filesystem::path& path) {
    std::ifstream in(path);
    if(!in) throw std::runtime_error("Cannot open pose list " + cat::path_utf8(path));
    std::vector<Pose> poses;
    std::string line;
    while(std::getline(in, line)) {
        if(line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        Pose p;
        if(!(row >> p.name >> p.x >> p.y >> p.z >> p.yaw >> p.pitch)) throw std::runtime_error("Malformed pose line: " + line);
        poses.push_back(p);
    }
    if(poses.empty()) throw std::runtime_error("Pose list is empty");
    return poses;
}
void apply_features(cat::RenderFeatures& f, std::string_view list) {
    std::string item;
    std::istringstream in{std::string(list)};
    while(std::getline(in, item, ',')) {
        if(item.size() < 2 || (item[0] != '+' && item[0] != '-')) throw std::runtime_error("--features items must be +name or -name");
        const bool on = item[0] == '+';
        const auto name = item.substr(1);
        bool* target = name == "sky" ? &f.sky : name == "world" ? &f.world : name == "materials" ? &f.materials : name == "clouds" ? &f.clouds
                     : name == "translucent" ? &f.translucent : name == "lightmap" ? &f.lightmap : name == "fog" ? &f.fog
                     : name == "posterize" ? &f.posterize : name == "culling" ? &f.culling : name == "dlights" ? &f.dynamic_lights : nullptr;
        if(!target) throw std::runtime_error("Unknown feature: " + name);
        *target = on;
    }
}
std::string json_escape(const std::string& s) {
    std::string o;
    for(char c : s) { if(c == '"' || c == '\\') o += '\\'; o += c; }
    return o;
}
std::string json_list(const std::vector<std::string>& items, const char* indent) {
    std::string o = "[";
    for(std::size_t i = 0; i < items.size(); ++i) o += std::string(i ? "," : "") + "\n" + indent + "  \"" + json_escape(items[i]) + "\"";
    return o + (items.empty() ? "]" : std::string("\n") + indent + "]");
}
// Detected capabilities, the selected render path and the objects it created.
std::string vulkan_json(const cat::DeviceReport& d, const char* indent) {
    std::ostringstream o;
    const std::string in = indent, in2 = in + "  ";
    auto b = [](bool v) { return v ? "true" : "false"; };
    o << "{\n" << in2 << "\"baselineApi\": \"1.2\",\n" << in2 << "\"device\": \"" << json_escape(d.device) << "\",\n" << in2 << "\"deviceType\": \"" << d.device_type
      << "\",\n" << in2 << "\"driver\": \"" << json_escape(d.driver_name) << "\",\n" << in2 << "\"driverInfo\": \"" << json_escape(d.driver_info)
      << "\",\n" << in2 << "\"deviceApi\": \"" << d.api << "\",\n" << in2 << "\"loaderApi\": \"" << d.loader_api << "\",\n" << in2 << "\"instanceApi\": \"" << d.instance_api
      << "\",\n" << in2 << "\"negotiatedApi\": \"" << d.negotiated_api << "\",\n" << in2 << "\"path\": \"" << d.path.tier() << "\",\n" << in2 << "\"features\": {";
    const auto& table = cat::vk::feature_table();
    for(std::size_t i = 0; i < table.size(); ++i) {
        const auto& f = table[i];
        o << (i ? "," : "") << "\n" << in2 << "  \"" << f.name << "\": {\"selected\": \"" << cat::vk::source_name(d.path.source[i]) << "\", \"extension\": \"" << f.extension
          << "\", \"extensionAdvertised\": " << b(d.caps.has_extension(f.extension)) << ", \"coreBit\": " << b(d.caps.core_bit[i])
          << ", \"extensionBit\": " << b(d.caps.extension_bit[i]) << ", \"fallback\": \"" << f.fallback << "\"}";
    }
    o << "\n" << in2 << "},\n" << in2 << "\"hostImageCopyChecks\": {\"format\": " << b(d.caps.host_copy_format) << ", \"layout\": " << b(d.caps.host_copy_layout)
      << ", \"optimalDeviceAccess\": " << b(d.caps.host_copy_optimal) << "},\n" << in2 << "\"deviceExtensionsAdvertised\": " << d.caps.extensions.size()
      << ",\n" << in2 << "\"enabledExtensions\": " << json_list(d.enabled_extensions, in2.c_str()) << ",\n" << in2 << "\"selection\": " << json_list(d.path.notes, in2.c_str())
      << ",\n" << in2 << "\"objects\": {\"renderPasses\": " << d.render_passes << ", \"framebuffers\": " << d.framebuffers << ", \"pipelines\": " << d.pipelines
      << ", \"materialPipelines\": " << d.material_pipelines << ", \"descriptorSets\": " << d.descriptor_sets << ", \"stagingTextureBytes\": " << d.staging_texture_bytes
      << ", \"hostCopyTextureBytes\": " << d.host_copy_texture_bytes << "},\n" << in2 << "\"anisotropy\": " << (d.anisotropy ? d.max_anisotropy : 0)
      << ", \"colorFormat\": \"" << d.color_format << "\", \"depthFormat\": \"" << d.depth_format << "\"\n" << in << "}";
    return o.str();
}
// --walk script: "keys[@dx[/dy]]:ticks,..." -> one TickInput per simulation tick.
std::vector<cat::physics::TickInput> parse_walk(const std::string& script) {
    using namespace cat::physics;
    std::vector<TickInput> tape;
    std::istringstream segments(script);
    std::string segment;
    while(std::getline(segments, segment, ',')) {
        const auto colon = segment.rfind(':');
        if(colon == std::string::npos) throw std::runtime_error("--walk segment needs ':ticks': " + segment);
        const int ticks = std::stoi(segment.substr(colon + 1));
        if(ticks <= 0 || ticks > 120000) throw std::runtime_error("--walk tick count out of range: " + segment);
        std::string keys = segment.substr(0, colon);
        TickInput in;
        if(const auto at = keys.find('@'); at != std::string::npos) {
            const std::string look = keys.substr(at + 1);
            keys.resize(at);
            const auto slash = look.find('/');
            in.look_dx = real(look.substr(0, slash));
            if(slash != std::string::npos) in.look_dy = real(look.substr(slash + 1));
        }
        std::istringstream names(keys);
        std::string name;
        while(std::getline(names, name, '+')) {
            if(name == "w") in.keys |= key_forward; else if(name == "s") in.keys |= key_back; else if(name == "a") in.keys |= key_left;
            else if(name == "d") in.keys |= key_right; else if(name == "jump" || name == "space") in.keys |= key_jump;
            else if(name == "crouch" || name == "ctrl") in.keys |= key_crouch; else if(name == "sprint" || name == "shift") in.keys |= key_sprint;
            else if(name == "slide" || name == "c") in.keys |= key_slide; else if(name == "fire") in.keys |= cat::combat::key_fire;
            else if(name == "reload" || name == "r") in.keys |= cat::combat::key_reload; else if(name == "swap" || name == "q") in.keys |= cat::combat::key_switch;
            else if(!name.empty() && name != "-") throw std::runtime_error("--walk: unknown key '" + name + "'");
        }
        tape.insert(tape.end(), static_cast<std::size_t>(ticks), in);
    }
    if(tape.empty()) throw std::runtime_error("--walk script is empty");
    return tape;
}
const char* stance_name(cat::physics::Stance s) {
    using cat::physics::Stance;
    switch(s) { case Stance::run: return "run"; case Stance::sprint: return "sprint"; case Stance::crouch: return "crouch"; case Stance::slide: return "slide";
                case Stance::air_control: return "air"; case Stance::air_momentum: return "air-momentum"; }
    return "?";
}
// Legacy VOID_MAPS (index.html 8181): internal id, wire id, user-facing title, in menu order.
struct MapEntry { const char* id; const char* wire_id; const char* title; };
constexpr MapEntry kMaps[] = {{"helios-crucible", "helios-crucible-v1", "HELIOS CRUCIBLE"}, {"bastion-keep", "bastion-keep-v1", "BASTION KEEP"},
                              {"reactor-bastion", "reactor-bastion-v1", "REACTOR BASTION"}, {"q3-gallery", "q3-gallery-v1", "THE ARSENAL"}};
const MapEntry* find_map(std::string_view id) { for(const auto& m : kMaps) if(id == m.id) return &m; return nullptr; }
// Legacy menu preview camera (legacy/index.html 16569) and Step 02 reference_config.json.
constexpr Pose kHeliosPreview{"preview", 78, 42, 0, 2.45, .15};

// ---- N5: weapon models / effect atlas (assets/n5, written by tools/n5/legacy_weapon_assets.cjs) ----
static_assert(sizeof(cat::hud::Vertex) == sizeof(cat::HudVertex) && sizeof(cat::hud::SolidVertex) == sizeof(cat::SolidVertex) && sizeof(cat::fx::Vertex) == 48);
std::filesystem::path overlay_asset_dir(const std::filesystem::path& asset_root) {
    for(const auto& dir : {asset_root.parent_path() / "n5", asset_root / ".." / "n5"}) if(std::filesystem::exists(dir / "fx-atlas.png")) return dir;
    return {};
}
cat::OverlayAssets load_overlay_assets(const std::filesystem::path& dir) {
    cat::OverlayAssets a;
    a.fx_atlas = cat::scene::load_image(dir / "fx-atlas.png");
    if(a.fx_atlas.width != 1024 || a.fx_atlas.height != 1024) throw std::runtime_error("fx-atlas.png must be 1024 x 1024 (8 x 8 tiles)");
    for(const char* stem : cat::fx::kGunFile) {
        const auto base = dir / "weapons" / stem;
        std::ifstream in(std::filesystem::path(base) += ".gun", std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::uint32_t count = 0;
        if(bytes.size() < 24 || std::string_view(bytes.data(), 8) != "CATGUN01") throw std::runtime_error("Not a weapon model: " + cat::path_utf8(base) + ".gun");
        std::memcpy(&count, bytes.data() + 8, 4);
        if(count == 0 || count % 3 != 0 || bytes.size() != 24 + std::size_t(count) * 48) throw std::runtime_error("Weapon model has an unexpected size: " + cat::path_utf8(base) + ".gun");
        cat::WeaponModel m;
        m.vertices.resize(std::size_t(count) * 12);
        std::memcpy(m.vertices.data(), bytes.data() + 24, m.vertices.size() * 4);
        m.base = cat::scene::load_image(std::filesystem::path(base) += "_base.jpg");
        m.metal_rough = cat::scene::load_image(std::filesystem::path(base) += "_mr.jpg");
        m.normal = cat::scene::load_image(std::filesystem::path(base) += "_normal.jpg");
        a.weapons.push_back(std::move(m));
    }
    return a;
}
// ---- N6: creature meshes (assets/n6, written by tools/n6/legacy_enemy_assets.cjs), their textures (assets/n1) ----
static_assert(sizeof(cat::fx::EnemyInstance) == 112);
std::filesystem::path enemy_asset_dir(const std::filesystem::path& asset_root) {
    for(const auto& dir : {asset_root.parent_path() / "n6", asset_root / ".." / "n6"}) if(std::filesystem::exists(dir / "marine.emesh")) return dir;
    return {};
}
cat::EnemyModel load_enemy_mesh(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::uint32_t count = 0, flags = 0;
    if(bytes.size() < 80 || std::string_view(bytes.data(), 8) != "CATENM01") throw std::runtime_error("Not a creature mesh: " + cat::path_utf8(file));
    std::memcpy(&count, bytes.data() + 8, 4); std::memcpy(&flags, bytes.data() + 12, 4);
    if(count == 0 || count % 3 != 0 || bytes.size() != 80 + std::size_t(count) * 48 + ((flags & 1U) ? std::size_t(count) * 2 : 0)) throw std::runtime_error("Creature mesh has an unexpected size: " + cat::path_utf8(file));
    cat::EnemyModel m;
    std::memcpy(m.rig.data(), bytes.data() + 16, 48);
    m.vertices.resize(std::size_t(count) * 12);
    std::memcpy(m.vertices.data(), bytes.data() + 80, m.vertices.size() * 4);
    return m;
}
cat::EnemyAssets load_enemy_assets(const std::filesystem::path& dir, const std::filesystem::path& asset_root) {
    cat::EnemyAssets a;
    for(std::size_t i = 0; i < cat::fx::kEnemyFile.size(); ++i) {
        auto m = load_enemy_mesh(dir / "enemies" / (std::string(cat::fx::kEnemyFile[i]) + ".emesh"));
        const auto base = asset_root / "models" / "enemies" / cat::fx::kEnemyName[i];
        m.base = cat::scene::load_image(std::filesystem::path(base) += "-texture-0.jpg");
        m.metal_rough = cat::scene::load_image(std::filesystem::path(base) += "-texture-1.jpg");
        m.normal = cat::scene::load_image(std::filesystem::path(base) += "-texture-2.jpg");
        a.types.push_back(std::move(m));
    }
    a.marine = load_enemy_mesh(dir / "marine.emesh");
    a.marine.base = cat::scene::load_image(asset_root / "embedded" / "assets" / "textures" / "cat-faces.png");
    return a;
}
enum class EnemyMode { waves, practice, none };
// One player with weapons on one map: simulation (combat::Arena + combat::Horde), presentation
// (fx::WeaponFx + fx::EnemyFx), HUD.
struct Session {
    const cat::physics::CollisionWorld* world;
    cat::combat::Arena arena;
    cat::fx::WeaponFx effects;
    cat::fx::Frame frame;
    cat::hud::Canvas canvas{1, 1};
    std::vector<std::uint32_t> mark_indices, main_indices, gun_indices;
    std::vector<cat::hud::SolidVertex> solid;
    std::vector<cat::DynamicLight> lights;   // N5.1: the frame's best 16 lights, for FrameView
    // N6: enemies. `horde` exists in wave play only; it is attached to the arena for its whole life.
    std::optional<cat::combat::Horde> horde;
    cat::fx::EnemyFx enemy_fx;
    cat::fx::EnemyFrame enemy_frame;
    std::vector<cat::EnemyBatch> batches;
    cat::fx::BuildOptions enemy_options;
    bool light_grid{true}, god{}, third_person{};
    int highest_wave{}; std::size_t most_enemies{}, most_enemy_shots{}, most_instances{}, most_outlines{}; std::uint64_t enemy_impacts{}, corpses_seen{}, gibbed{};
    double fov, aspect_{16.0 / 9};
    bool practice{true};
    bool show_hud{true};
    double hit_marker{}, notice_time{}, retarget_in{-1};
    bool hit_kill{}, hit_head{};
    std::string notice, map_title;
    std::string pause_title, pause_help;   // N6.5: interactive pause panel (empty = running)
    std::uint64_t volleys{}, pellets{}, pellet_hits{}, target_hits{}, head_hits{}, detonations{}, reloads{}, swaps{}, deaths_seen{};
    std::uint32_t waves_seen{}; bool cleared_seen{};
    double damage_dealt{}, damage_taken{}, healed{};
    Session(const cat::physics::CollisionWorld& w, std::uint32_t seed, double horizontal_fov, cat::nav::NavGraph* graph = nullptr)
        : world(&w), arena(w, seed), effects(w, seed * 31U + 17U), enemy_fx(effects, seed * 131U + 7U), fov(horizontal_fov) {
        if(graph) { horde.emplace(arena, *graph); arena.attach(&*horde); }
        // effects tick inside Arena::step, where the legacy update() runs WFX.update / EFX.update
        arena.set_presentation_hook([this] { if(horde) enemy_fx.tick(arena, *horde, fov, aspect_); else effects.tick(arena, fov, aspect_); });
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    // Up to four targets in front of the player, on the player's floor, with a clear line of fire.
    void place_targets() {
        arena.clear_targets();
        if(!practice) return;
        const auto& p = arena.player().state();
        struct Row { double d, side; cat::combat::TargetClass cls; double hp; };
        const Row rows[] = {{6, 0, cat::combat::TargetClass::standard, 150}, {9, 1.3, cat::combat::TargetClass::heavy, 300}, {12, -1.3, cat::combat::TargetClass::fast, 110}, {15, .5, cat::combat::TargetClass::standard, 150}};
        std::vector<std::array<double, 5>> best;
        for(const double turn : {0.0, .7853981633974483, -.7853981633974483, 1.5707963267948966, -1.5707963267948966, 2.356194490192345, -2.356194490192345, 3.141592653589793}) {
            const double a = p.a + turn, ca = std::cos(a), sa = std::sin(a);
            std::vector<std::array<double, 5>> found;
            for(const auto& r : rows) {
                const double x = p.x + ca * r.d - sa * r.side, y = p.y + sa * r.d + ca * r.side;
                const auto support = world->support(x, y, p.z, .6);
                if(!support || std::fabs(support->z - p.z) > .6 || !world->capsule_clear(x, y, support->z, 2.4, .75)) continue;
                if(std::isfinite(world->shot_collision_t(p.x, p.y, p.z + 1.5, x, y, support->z + 1.2, .1))) continue;
                found.push_back({x, y, support->z, static_cast<double>(static_cast<int>(r.cls)), r.hp});
            }
            if(found.size() > best.size()) best = found;
            if(best.size() == std::size(rows)) break;
        }
        for(const auto& t : best) arena.add_target(t[0], t[1], t[2], static_cast<cat::combat::TargetClass>(static_cast<int>(t[3])), t[4]);
        retarget_in = -1;
    }
    void step(const cat::physics::TickInput& in, double aspect) {
        if(god) arena.set_health(cat::combat::kMaxHealth);
        aspect_ = aspect;
        arena.step(in);   // ticks the effects too (presentation hook)
        if(horde) {
            highest_wave = std::max(highest_wave, horde->wave_number()); most_enemies = std::max(most_enemies, arena.targets().size()); most_enemy_shots = std::max(most_enemy_shots, horde->shots().size());
            enemy_impacts += horde->impacts().size();
            for(const auto& d : horde->deaths()) { corpses_seen += !d.gibbed; gibbed += d.gibbed; }
        }
        const auto& e = arena.events();
        const double dt = cat::physics::kTick;
        hit_marker = std::max(0.0, hit_marker - dt * 4); notice_time = std::max(0.0, notice_time - dt);
        if(e.fired_weapon >= 0) { ++volleys; pellets += e.shots.size(); for(const auto& s : e.shots) pellet_hits += s.hit; }
        for(const auto& d : e.damage) { ++target_hits; head_hits += d.head; damage_dealt += d.amount; hit_marker = 1; hit_kill = d.killed; hit_head = d.head; }
        detonations += e.blasts.size(); reloads += e.reload_started; swaps += e.switched; damage_taken += e.player_damage; healed += e.healed;
        if(e.reroll >= 0) { notice = std::string("FRAG - NEW WEAPON: ") + cat::combat::weapons()[static_cast<std::size_t>(e.reroll)].name; notice_time = 2.4; }
        if(horde) {   // announce(): beginWave "WAVE n · total ENEMIES · +p% STATS" (2.5 s), tickWaves "WAVE n CLEARED!" (3.25 s)
            const bool cleared = horde->wave_cooldown() >= 0;
            if(horde->waves_begun() != waves_seen) { waves_seen = horde->waves_begun(); announce_wave(); }
            else if(cleared && !cleared_seen) { notice = "WAVE " + std::to_string(horde->wave_number()) + " CLEARED!"; notice_time = 3.25; }
            cleared_seen = cleared;
        }
        if(arena.deaths() != deaths_seen) { deaths_seen = arena.deaths(); notice = horde ? "YOU DIED - WAVE 1 RESTARTS" : "YOU DIED - RESPAWNED"; notice_time = 2.4; if(!horde) place_targets(); }
        if(practice && arena.targets().empty()) {
            if(retarget_in < 0) retarget_in = 2.5;
            retarget_in -= dt;
            if(retarget_in <= 0) place_targets();
        }
    }
    void announce_wave() {
        if(!horde) return;
        waves_seen = horde->waves_begun();
        notice = "WAVE " + std::to_string(horde->wave_number()) + " - " + std::to_string(horde->wave_total()) + " ENEMIES - +" + std::to_string(static_cast<int>(std::lround((horde->scale() - 1) * 100))) + "% STATS";
        notice_time = 2.5;
    }
    static cat::OverlayCamera camera(const cat::fx::Camera& c) {
        cat::OverlayCamera o;
        o.pos[0] = static_cast<float>(c.x); o.pos[1] = static_cast<float>(c.y); o.pos[2] = static_cast<float>(c.z);
        for(std::size_t i = 0; i < 3; ++i) { o.forward[i] = static_cast<float>(c.forward[i]); o.right[i] = static_cast<float>(c.right[i]); o.up[i] = static_cast<float>(c.up[i]); }
        o.tan_half = static_cast<float>(c.tan_half); o.aspect = static_cast<float>(c.aspect);
        return o;
    }
    // The returned overlay points into this session; it is valid until the next call.
    cat::Overlay build(std::pair<std::uint32_t, std::uint32_t> size) {
        const double aspect = double(size.first) / double(size.second);
        effects.build(arena, fov, aspect, frame);
        batches.clear();
        if(horde) {   // creatures: poses, instance lists, enemy projectiles / glows / shadows (adds quads and lights to the frame)
            enemy_options.fov_deg = fov; enemy_options.aspect = aspect; enemy_options.local_marine = third_person;
            enemy_fx.build(arena, *horde, enemy_options, frame, enemy_frame);
            const auto add = [&](const std::vector<cat::fx::EnemyInstance>& list, int mesh, cat::EnemyPass pass) { if(!list.empty()) batches.push_back({mesh, pass, list[0].pos, list.size()}); };
            for(std::size_t t = 0; t < 20; ++t) add(enemy_frame.solid[t], static_cast<int>(t), cat::EnemyPass::solid);
            add(enemy_frame.marines, 20, cat::EnemyPass::marine);
            for(std::size_t t = 0; t < 20; ++t) add(enemy_frame.shadow[t], static_cast<int>(t), cat::EnemyPass::shadow);
            std::size_t outlines = 0;
            for(std::size_t t = 0; t < 20; ++t) { add(enemy_frame.xray[t], static_cast<int>(t), cat::EnemyPass::outline); outlines += enemy_frame.xray[t].size(); }
            most_instances = std::max(most_instances, enemy_frame.instances()); most_outlines = std::max(most_outlines, outlines);
        }
        mark_indices = frame.mark.indices(); main_indices = frame.main.indices(); gun_indices = frame.gun.indices();
        lights.clear();
        for(const auto& l : cat::fx::select_lights(frame.lights, frame.camera.x, frame.camera.y, frame.camera.z))
            lights.push_back({static_cast<float>(l.x), static_cast<float>(l.y), static_cast<float>(l.z), static_cast<float>(l.radius), static_cast<float>(l.r), static_cast<float>(l.g), static_cast<float>(l.b)});
        cat::Overlay o;
        o.camera = camera(frame.camera);
        if(third_person) for(std::size_t k = 0; k < 3; ++k) o.camera.pos[k] += static_cast<float>(-3.6 * frame.camera.forward[k] + .55 * frame.camera.up[k]);
        auto batch = [](const cat::fx::Batch& b, const std::vector<std::uint32_t>& idx) { return cat::OverlayBatch{b.vertices.empty() ? nullptr : &b.vertices[0].x, b.vertices.size(), idx.data(), idx.size()}; };
        o.mark = batch(frame.mark, mark_indices); o.main = batch(frame.main, main_indices); o.gun_fx = batch(frame.gun, gun_indices);
        o.enemies = batches.empty() ? nullptr : batches.data(); o.enemy_batches = batches.size();
        o.shadow_dir[0] = enemy_frame.shadow_dir[0]; o.shadow_dir[1] = enemy_frame.shadow_dir[1]; o.light_grid = light_grid;
        o.gun_visible = frame.gun_view.visible && !third_person; o.gun = frame.gun_view.weapon; o.gun_camera = camera(frame.gun_view.camera);
        for(std::size_t i = 0; i < 3; ++i) o.gun_glow[i] = static_cast<float>(frame.gun_view.glow[i]);
        o.atlas = frame.atlas.empty() ? nullptr : frame.atlas.data(); o.atlas_vertices = frame.atlas.size() / 12;
        cat::hud::target_boxes(arena.targets(), solid);
        o.solid = solid.empty() ? nullptr : reinterpret_cast<const cat::SolidVertex*>(solid.data()); o.solid_vertices = solid.size();
        const auto& w = cat::combat::weapons();
        const auto& d = w[static_cast<std::size_t>(arena.current_weapon())];
        cat::hud::State h;
        h.health = arena.health(); h.ammo = arena.ammo(arena.slot()); h.clip = d.clip; h.reloading = arena.reloading(); h.reload_time = d.reload;
        h.weapon = d.name; h.other_weapon = w[static_cast<std::size_t>(arena.weapon(1 - arena.slot()))].name; h.weapon_color = d.color; h.map_title = map_title;
        h.frags = arena.frags(); h.deaths = arena.deaths(); h.targets = horde ? 0 : static_cast<int>(arena.targets().size());
        if(horde) {
            h.wave = horde->wave_number(); h.wave_remaining = horde->remaining(); h.enemies_alive = static_cast<int>(arena.targets().size()); h.wave_cooldown = horde->wave_cooldown();
            h.final_five = enemy_frame.xray_on; h.burning = arena.burn_remaining();
        }
        h.damage_flash = arena.damage_flash(); h.hit_marker = hit_marker; h.hit_was_kill = hit_kill; h.hit_was_head = hit_head; h.respawn_grace = arena.respawn_grace();
        h.notice = notice; h.notice_alpha = notice_time;
        h.pause_title = pause_title; h.pause_help = pause_help;
        canvas = cat::hud::Canvas(double(size.first), double(size.second));
        if(show_hud) cat::hud::build(canvas, h);
        o.hud = canvas.vertices().empty() ? nullptr : reinterpret_cast<const cat::HudVertex*>(canvas.vertices().data()); o.hud_vertices = canvas.vertices().size();
        return o;
    }
    // After build(): hands the frame's lights to the map renderer.
    void light(cat::FrameView& view) const { view.lights = lights.empty() ? nullptr : lights.data(); view.light_count = lights.size(); }
    std::string json() const {
        std::ostringstream o;
        o << "{\"health\": " << arena.health() << ", \"weapons\": [" << arena.weapon(0) << ", " << arena.weapon(1) << "], \"slot\": " << arena.slot() << ", \"magazines\": [" << arena.ammo(0) << ", " << arena.ammo(1)
          << "], \"volleys\": " << volleys << ", \"pellets\": " << pellets << ", \"pelletHits\": " << pellet_hits << ", \"targetHits\": " << target_hits << ", \"headHits\": " << head_hits
          << ", \"damageDealt\": " << damage_dealt << ", \"detonations\": " << detonations << ", \"frags\": " << arena.frags() << ", \"deaths\": " << arena.deaths() << ", \"damageTaken\": " << damage_taken
          << ", \"healed\": " << healed << ", \"reloads\": " << reloads << ", \"swaps\": " << swaps << ", \"targetsAlive\": " << arena.targets().size()
          << ", \"gameplayRandomDraws\": " << arena.rng().calls() << ", \"cosmeticRandomDraws\": " << effects.rng().calls();
        if(horde) {
            const auto& n = horde->nav_stats();
            o << ", \"waves\": {\"wave\": " << horde->wave_number() << ", \"highestWave\": " << highest_wave << ", \"total\": " << horde->wave_total() << ", \"spawned\": " << horde->wave_spawned() << ", \"kills\": " << horde->wave_kills()
              << ", \"remaining\": " << horde->remaining() << ", \"scale\": " << horde->scale() << ", \"enemiesAlive\": " << arena.targets().size() << ", \"mostEnemies\": " << most_enemies
              << ", \"enemyShots\": " << horde->shots().size() << ", \"mostEnemyShots\": " << most_enemy_shots << ", \"enemyShotImpacts\": " << enemy_impacts << ", \"corpses\": " << horde->corpses().size()
              << ", \"deathAnimations\": " << corpses_seen << ", \"gibbed\": " << gibbed << ", \"routes\": " << n.success << ", \"routeQueries\": " << n.queries << ", \"sharedRoutes\": " << n.shared
              << ", \"mostCreatureInstances\": " << most_instances << ", \"mostOutlines\": " << most_outlines << ", \"animationRandomDraws\": " << enemy_fx.rng().calls() << "}";
        }
        o << "}";
        return o.str();
    }
};

// ---- N7: menus, HUD, settings and sound ----
std::filesystem::path n7_asset_dir(const std::filesystem::path& asset_root) {
    for(const auto& dir : {asset_root.parent_path() / "n7", asset_root / ".." / "n7"}) if(std::filesystem::exists(dir / "N7_ASSET_MANIFEST.json")) return dir;
    return {};
}
struct UiKit { cat::ui::Atlas atlas; cat::ui::Region background; };
UiKit load_ui_kit(const std::filesystem::path& dir) {
    const auto f = dir / "fonts";
    UiKit k{cat::ui::Atlas::build({f / "LiberationSerif-Bold.ttf", f / "LiberationSans-Bold.ttf", f / "LiberationMono-Bold.ttf", f / "LiberationSerif-Regular.ttf"}, f / "DejaVuSans-Bold.ttf"), {}};
    k.background = k.atlas.add_image(cat::scene::load_image(dir / "ui" / "feral-bg.png"));   // --feral-bg (the legacy menu backdrop)
    return k;
}
// The map cards of the legacy maps page (titles of VOID_MAPS; tag line and description from the card table, index.html).
std::vector<cat::ui::MapEntry> menu_maps() {
    return {{"helios-crucible", "HELIOS CRUCIBLE", "Three levels · Live reactor", "A burning heart of iron. Cross the gantries above the crucible and descend into its coolant halls."},
            {"bastion-keep", "BASTION KEEP", "Three levels · Fortress", "A broken military stronghold. Fight through fortified chambers, bridges and the shattered upper keep."},
            {"reactor-bastion", "REACTOR BASTION", "Industrial · Survival", "The original reactor fortress. Interconnected decks, hard cover and close-quarters routes."},
            {"q3-gallery", "THE ARSENAL", "Exhibition · Combat lab", "The Arsenal: an arena of experimental materials, machinery and combat spaces."}};
}
// The menu preview shot of each map (legacy previewShot cameras: x, y, z, yaw, pitch).
Pose preview_pose(std::string_view map) {
    if(map == "bastion-keep") return {"preview", 78, 38, 0, 2.36, .1};
    if(map == "reactor-bastion") return {"preview", 78.5, 60.5, 0, 3.14, .05};
    if(map == "q3-gallery") return {"preview", 42, 46, 0, .73, .12};
    return kHeliosPreview;
}
// The stance line (q3 movement: stance + slopeTag, 12209-12211).
std::string stance_label(const cat::physics::PlayerController& pc) {
    using cat::physics::Stance;
    std::string s;
    switch(pc.stance()) {
        case Stance::air_control: s = "AIR STRAFE · CONTROL"; break;
        case Stance::air_momentum: s = "AIR STRAFE · MOMENTUM"; break;
        case Stance::slide: s = "CROUCH SLIDE"; break;
        case Stance::crouch: s = "CROUCH"; break;
        case Stance::sprint: s = "SPRINT"; break;
        case Stance::run: s = "RUN"; break;
    }
    if(pc.slope_angle() > .085) s += " · UPHILL"; else if(pc.slope_angle() < -.085) s += " · DOWNHILL";
    return s;
}
cat::ui::HudInfo hud_info(Session& s) {
    cat::ui::HudInfo h;
    auto& a = s.arena;
    const auto& w = cat::combat::weapons();
    const auto& d = w[static_cast<std::size_t>(a.current_weapon())];
    const auto& p = a.player().state();
    h.health = a.health(); h.speed = std::hypot(p.vx, p.vy);
    h.ammo = a.ammo(a.slot()); h.clip = d.clip; h.reloading = a.reloading(); h.reload_time = d.reload;
    h.weapon = d.name; h.other_weapon = w[static_cast<std::size_t>(a.weapon(1 - a.slot()))].name; h.map_title = s.map_title;
    h.stance = stance_label(a.player());
    h.frags = a.frags(); h.deaths = a.deaths();
    if(s.horde) {
        h.wave = s.horde->wave_number(); h.wave_remaining = s.horde->remaining(); h.enemies_alive = static_cast<int>(a.targets().size()); h.wave_cooldown = s.horde->wave_cooldown();
        h.final_five = s.enemy_frame.xray_on; h.burning = a.burn_remaining();
    }
    h.damage_flash = a.damage_flash(); h.hit_marker = s.hit_marker; h.hit_was_kill = s.hit_kill; h.hit_was_head = s.hit_head; h.respawn_grace = a.respawn_grace();
    h.notice = s.notice; h.notice_alpha = s.notice_time;
    for(std::size_t at = 0; (at = h.notice.find(" - ", at)) != std::string::npos;) h.notice.replace(at, 3, " · ");   // the N5 bitmap HUD has no '·'; the legacy text does
    return h;
}
std::string mib(std::uint64_t bytes) { char b[32]; std::snprintf(b, sizeof b, "%.1f MiB", double(bytes) / 1048576.0); return b; }
// The performance overlay rows (index.html 627 #perfOverlay), each behind its settings toggle.
std::vector<cat::ui::PerfLine> perf_lines(const cat::GameSettings& gs, const cat::PerfMeter::Summary& p, const cat::RenderStats& st, bool gpu_timer) {
    std::vector<cat::ui::PerfLine> out;
    char b[48];
    auto fps = [&](double v) { if(v <= 0) return std::string("--"); std::snprintf(b, sizeof b, "%.0f", v); return std::string(b); };
    if(gs.flag("perfFps")) {
        out.push_back({"FPS LIVE", fps(p.fps_live)}); out.push_back({"FPS AVG · 60s", fps(p.fps_avg)});
        out.push_back({"1% LOW", fps(p.low1)}); out.push_back({"0.1% LOW", fps(p.low01)}); out.push_back({"HITCHES 25ms+", std::to_string(p.hitches)});
    }
    if(gs.flag("perfCpu")) { std::snprintf(b, sizeof b, "%.2f ms", p.cpu_ms); out.push_back({"CPU", p.cpu_ms > 0 ? b : "--"}); }
    if(gs.flag("perfGpu")) { std::snprintf(b, sizeof b, "%.2f ms", st.gpu_ms); out.push_back({"GPU", !gpu_timer ? "N/A (unsupported)" : st.gpu_ms < 0 ? "WAITING" : b}); }
    const auto mem = (gs.flag("perfRam") || gs.flag("perfAppMem")) ? cat::process_memory() : cat::ProcessMemory{};
    if(gs.flag("perfRam")) out.push_back({"PROCESS RAM", mem.ok ? mib(mem.current) : "N/A"});
    if(gs.flag("perfAppMem")) out.push_back({"PEAK RAM*", mem.ok && mem.peak ? mib(mem.peak) : "N/A"});
    if(gs.flag("perfVram")) out.push_back({"GPU ALLOC~", mib(st.tracked_bytes)});
    return out;
}
// UI pixels per legacy CSS pixel: the display scale, at least the 1280 x 720 fit, at most what keeps a
// 640 x 360 CSS viewport (the menus' smallest layout).
double ui_scale(double width, double height, double display_scale) {
    const double fit = std::min(width / 1280.0, height / 720.0);
    return std::clamp(std::max(display_scale, fit), .5, std::max(.5, std::min(width / 640.0, height / 360.0)));
}
static_assert(sizeof(cat::ui::Vertex) == sizeof(cat::UiVertex));
void attach_ui(cat::Overlay& o, const cat::ui::Canvas& c) {
    o.ui = c.vertices().empty() ? nullptr : reinterpret_cast<const cat::UiVertex*>(c.vertices().data()); o.ui_vertices = c.vertices().size();
}
// The legacy keyboard handling of the menus (v0.18: arrows, Enter, Esc, Tab, Ctrl+Tab, PageUp / PageDown, Home / End).
// N8 name tags: the point above another player's head on screen (nullopt behind the camera).
std::optional<cat::ui::CoopTag> project_tag(const cat::fx::Camera& cam, const cat::physics::CollisionWorld& world, double x, double y, double z, bool downed, double width, double height) {
    const double hz = z + (downed ? .9 : 2.15);
    const double dx = x - cam.x, dy = y - cam.y, dz = hz - cam.z;
    const double depth = dx * cam.forward[0] + dy * cam.forward[1] + dz * cam.forward[2];
    if(depth < .3) return std::nullopt;
    const double sx = (dx * cam.right[0] + dy * cam.right[1] + dz * cam.right[2]) / (depth * cam.tan_half * cam.aspect);
    const double sy = (dx * cam.up[0] + dy * cam.up[1] + dz * cam.up[2]) / (depth * cam.tan_half);
    cat::ui::CoopTag t;
    t.x = (sx * .5 + .5) * width; t.y = (.5 - sy * .5) * height; t.distance = std::sqrt(dx * dx + dy * dy + dz * dz); t.downed = downed;
    t.occluded = std::isfinite(world.shot_collision_t(cam.x, cam.y, cam.z, x, y, hz, .05));
    return t;
}
// --relay-server: runs until the process is stopped (Ctrl+C / closing the console window).
int run_relay_server(const std::string& bind, int port) {
    cat::net::RelayServer relay;
    relay.set_log([](const std::string& line) { cat::log("info", "relay", line); });
    std::string error;
    if(!relay.start(bind, port, error)) { cat::log("error", "relay", "Cannot start the relay on " + bind + ":" + std::to_string(port) + ": " + error); return 1; }
    cat::log("info", "relay", "Cat Arena relay (" + std::string(cat::net::kBuildId) + ") on " + bind + ":" + std::to_string(relay.port()) + ": ws://<this-computer>:" + std::to_string(relay.port()) + "/ws, GET /rooms, GET /health");
    for(;;) std::this_thread::sleep_for(std::chrono::seconds(60));
}
std::optional<cat::ui::NavKey> nav_key(SDL_Scancode code, SDL_Keymod mod) {
    using cat::ui::NavKey;
    const bool ctrl = (mod & SDL_KMOD_CTRL) != 0, shift = (mod & SDL_KMOD_SHIFT) != 0;
    switch(code) {
        case SDL_SCANCODE_UP: return NavKey::up; case SDL_SCANCODE_DOWN: return NavKey::down;
        case SDL_SCANCODE_LEFT: return NavKey::left; case SDL_SCANCODE_RIGHT: return NavKey::right;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: return NavKey::accept;
        case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: case SDL_SCANCODE_AC_BACK: return NavKey::back;
        case SDL_SCANCODE_TAB: return ctrl ? (shift ? NavKey::prev_tab : NavKey::next_tab) : (shift ? NavKey::prev_item : NavKey::next_item);
        case SDL_SCANCODE_PAGEUP: return NavKey::page_up; case SDL_SCANCODE_PAGEDOWN: return NavKey::page_down;
        case SDL_SCANCODE_HOME: return NavKey::first; case SDL_SCANCODE_END: return NavKey::last;
        default: return std::nullopt;
    }
}
std::optional<cat::ui::NavKey> pad_nav(std::uint8_t button) {   // native addition: the legacy has no gamepad support
    using cat::ui::NavKey;
    switch(button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return NavKey::up; case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return NavKey::down;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return NavKey::left; case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return NavKey::right;
        case SDL_GAMEPAD_BUTTON_SOUTH: return NavKey::accept; case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_BACK: case SDL_GAMEPAD_BUTTON_START: return NavKey::back;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return NavKey::prev_tab; case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return NavKey::next_tab;
        default: return std::nullopt;
    }
}
}  // namespace

// SDL_main.h last: on Windows it does `#define main SDL_main` (SDL then supplies WinMain / main with UTF-8
// arguments), which would also rename the `main` members of fx::Frame and cat::Overlay used above.
#include <SDL3/SDL_main.h>
namespace {
bool g_error_dialog = false;   // N6.5: an interactive (double-clicked) run shows fatal errors in a message box
}
int main(int argc, char** argv) {
 try {
    // No SDL init, filesystem writes, or Vulkan calls on this path.
    for(int i = 1; i < argc; ++i) if(std::string_view(argv[i]) == "--headless-test") {
        bool ok = cat::self_test(); cat::log(ok ? "info" : "error", "self-test", ok ? "PASS" : "FAIL"); return ok ? 0 : 1;
    }
    // N8: the multiplayer relay alone (reference/multiplayer/server.js in C++), no window: --relay-server [--port N] [--bind ADDRESS]
    for(int i = 1; i < argc; ++i) if(std::string_view(argv[i]) == "--relay-server") {
        int port = cat::net::kDefaultPort; std::string bind = "0.0.0.0";
        for(int j = 1; j + 1 < argc; ++j) {
            if(std::string_view(argv[j]) == "--port") port = std::stoi(argv[j + 1]);
            if(std::string_view(argv[j]) == "--bind") bind = argv[j + 1];
        }
        if(const char* env = std::getenv("PORT"); env && *env) port = std::atoi(env);   // server.js: process.env.PORT
        return run_relay_server(bind, port);
    }
    cat::Settings settings; std::optional<int> cap; bool no_vsync = false, no_validation = false, triangle = false;
    int frames = 0, warmup = 1; std::optional<std::filesystem::path> config, shaders, assets, capture, capture_set, capture_dir, report;
    std::string map = "helios-crucible";
    std::optional<std::pair<std::uint32_t, std::uint32_t>> offscreen;
    Pose pose = kHeliosPreview; double fov = cat::scene::kDefaultFov, time = 0; bool fixed_time = false;
    cat::RenderFeatures features;
    cat::vk::Request vk_request; bool vk_info = false, resize_test = false; std::optional<std::filesystem::path> log_file; bool controls_selftest = false;
    bool fly = false, input_selftest = false, camera_given = false, map_cycle_test = false; int spawn_index = 0, capture_every = 60; std::optional<std::string> walk;
    bool hud_enabled = true; std::optional<Pose> player_pose; std::optional<std::filesystem::path> combat_fixture; std::string scenario_name; std::vector<double> capture_ticks;
    EnemyMode enemy_mode = EnemyMode::waves; bool enemies_given = false; int start_wave = 1, shadow_mode = 1; bool light_grid = true, third_person = false, god = false; std::optional<std::filesystem::path> enemy_fixture, enemy_lineup; std::string lineup_shot;
    bool overlay_enabled = true, practice_targets = true; std::optional<std::pair<int, int>> start_weapons; std::uint32_t game_seed = 1; bool seed_given = false;
    bool menu_selftest = false, coop_selftest = false, map_given = false, fov_given = false, force_touch = false; std::optional<std::filesystem::path> game_settings_file, ui_capture;   // N7
    for(int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        auto value = [&]() -> std::string_view { if(++i >= argc) throw std::runtime_error("Missing option value"); return argv[i]; };
        if(arg == "--no-vsync") no_vsync = true;
        else if(arg == "--no-validation") no_validation = true;
        else if(arg == "--frames") frames = number(value());
        else if(arg == "--frame-cap") cap = number(value());
        else if(arg == "--settings") config = cat::utf8_path(value());
        else if(arg == "--shaders") shaders = cat::utf8_path(value());
        else if(arg == "--assets") assets = cat::utf8_path(value());
        else if(arg == "--map") { map = value(); map_given = true; }
        else if(arg == "--triangle") triangle = true;
        else if(arg == "--camera") { pose = parse_pose(value()); camera_given = true; }
        else if(arg == "--map-cycle-test") map_cycle_test = true;
        else if(arg == "--list-maps") { for(const auto& m : kMaps) std::cout << m.id << '\t' << m.wire_id << "\tCAT ARENA: FERAL CLAW // " << m.title << '\n'; return 0; }
        else if(arg == "--fov") { fov = real(std::string(value())); fov_given = true; }
        else if(arg == "--menu-selftest") menu_selftest = true;
        else if(arg == "--coop-selftest") coop_selftest = true;   // N8: hosts through the menus, three in-process guests join over TCP
        else if(arg == "--game-settings") game_settings_file = cat::utf8_path(value());
        else if(arg == "--ui-capture") ui_capture = cat::utf8_path(value());
        else if(arg == "--touch") force_touch = true;
        else if(arg == "--time") { time = real(std::string(value())); fixed_time = true; }
        else if(arg == "--features") apply_features(features, value());
        else if(arg == "--debug-view") features.debug_view = number(value());
        else if(arg == "--warmup") warmup = number(value());
        else if(arg == "--capture") capture = cat::utf8_path(value());
        else if(arg == "--capture-set") capture_set = cat::utf8_path(value());
        else if(arg == "--capture-dir") capture_dir = cat::utf8_path(value());
        else if(arg == "--report") report = cat::utf8_path(value());
        else if(arg == "--vk-info") vk_info = true;
        else if(arg == "--fly") fly = true;
        else if(arg == "--input-selftest") input_selftest = true;
        else if(arg == "--spawn") spawn_index = std::stoi(std::string(value()));
        else if(arg == "--walk") walk = std::string(value());
        else if(arg == "--capture-every") capture_every = std::max(1, std::stoi(std::string(value())));
        else if(arg == "--resize-test") resize_test = true;
        else if(arg == "--log-file") log_file = cat::utf8_path(value());
        else if(arg == "--controls-selftest") controls_selftest = true;
        else if(arg == "--no-overlay") overlay_enabled = false;
        else if(arg == "--no-targets") practice_targets = false;
        else if(arg == "--enemies") {
            const auto v = value(); enemies_given = true;
            enemy_mode = v == "waves" ? EnemyMode::waves : v == "practice" ? EnemyMode::practice : v == "none" ? EnemyMode::none : throw std::runtime_error("--enemies expects waves, practice or none");
        }
        else if(arg == "--wave") { start_wave = number(value()); if(start_wave < 1 || start_wave > 60) throw std::runtime_error("--wave expects 1..60"); }
        else if(arg == "--shadows") { shadow_mode = number(value()); if(shadow_mode != 0 && shadow_mode != 1 && shadow_mode != 3) throw std::runtime_error("--shadows expects 0, 1 or 3"); }
        else if(arg == "--no-light-grid") light_grid = false;
        else if(arg == "--third-person") third_person = true;
        else if(arg == "--god") god = true;
        else if(arg == "--enemy-fixture") enemy_fixture = cat::utf8_path(value());
        else if(arg == "--enemy-lineup") enemy_lineup = cat::utf8_path(value());
        else if(arg == "--lineup-shot") lineup_shot = std::string(value());
        else if(arg == "--no-hud") hud_enabled = false;
        else if(arg == "--player-pose") player_pose = parse_pose(value());
        else if(arg == "--combat-fixture") combat_fixture = cat::utf8_path(value());
        else if(arg == "--scenario") scenario_name = std::string(value());
        else if(arg == "--capture-ticks") capture_ticks = numbers(value(), ',');
        else if(arg == "--seed") { game_seed = static_cast<std::uint32_t>(std::stoul(std::string(value()))); seed_given = true; }
        else if(arg == "--weapons") {
            auto v = numbers(value(), ',');
            if(v.size() != 2 || v[0] < 0 || v[1] < 0 || v[0] > 15 || v[1] > 15) throw std::runtime_error("--weapons expects two weapon indices 0..15, e.g. --weapons 4,2");
            start_weapons = std::pair{static_cast<int>(v[0]), static_cast<int>(v[1])};
        }
        else if(arg == "--list-weapons") {
            int index = 0;
            for(const auto& w : cat::combat::weapons())
                std::cout << index++ << '\t' << w.name << "\tdamage " << w.damage << "\tdelay " << w.delay << " s\tclip " << w.clip << "\treload " << w.reload << " s\tpellets " << w.pellets << "\tspread " << w.spread
                          << "\trange " << w.range << " m\t" << (w.kind == cat::combat::FireKind::hitscan ? "hitscan" : w.kind == cat::combat::FireKind::rocket ? "rocket" : w.kind == cat::combat::FireKind::grenade ? "grenade" : "rift bolt") << '\n';
            return 0;
        }
        else if(arg == "--vk-baseline") { vk_request.max_api = cat::vk::kBaselineApi; vk_request.allow_extensions = false; }
        else if(arg == "--vk-max-api") vk_request.max_api = cat::vk::parse_api(value());
        else if(arg == "--vk-no-extensions") vk_request.allow_extensions = false;
        else if(arg == "--vk-disable") {
            std::string item;
            std::istringstream in{std::string(value())};
            while(std::getline(in, item, ',')) vk_request.disable(cat::vk::parse_feature(item));
        }
        else if(arg == "--offscreen") {
            auto v = numbers(value(), 'x');
            if(v.size() != 2) throw std::runtime_error("--offscreen expects WIDTHxHEIGHT");
            offscreen = std::pair{static_cast<std::uint32_t>(v[0]), static_cast<std::uint32_t>(v[1])};
        }
        else throw std::runtime_error("Unknown option: " + std::string(arg));
    }
    if(config) { std::ifstream in(*config); if(!in) throw std::runtime_error("Cannot open settings file"); settings = cat::Settings::read(in); }
    if(cap) settings.frame_cap = *cap;
    if(no_vsync) settings.vsync = false;
    if(no_validation) settings.validation = false;
    settings.validate();
    if(features.debug_view < 0 || features.debug_view > 3) throw std::runtime_error("--debug-view must be 0..3");
    if((capture || capture_set) && !offscreen) throw std::runtime_error("--capture/--capture-set require --offscreen WxH");
    if(capture_set && !capture_dir) throw std::runtime_error("--capture-set requires --capture-dir");

    const char* base_cstr = SDL_GetBasePath();
    const std::filesystem::path base = base_cstr ? cat::utf8_path(base_cstr) : std::filesystem::current_path();
    const auto shader_dir = shaders.value_or(base / "assets/shaders");
    // N6.5: a run started by hand in a window (no automation options) pauses while the mouse is
    // released, shows fatal errors in a message box and, on Windows, writes CatArena.log next to the
    // executable (the Windows build has no console).
    const bool interactive = !offscreen && !vk_info && frames == 0 && !input_selftest && !map_cycle_test && !resize_test && !controls_selftest && !menu_selftest && !coop_selftest;
    // N7: a run started by hand opens the menus (and so does the menu self-test); the automation options
    // (--frames, self-tests, captures) keep the N6.5 shell, which plays at once with the N5 HUD.
    const bool menus = (interactive || menu_selftest || coop_selftest) && !triangle && !fly;
    g_error_dialog = interactive;
    // --controls-selftest drives the interactive controls (pause, resume, F11, F5, Esc twice) with synthetic SDL events.
    const bool pausing = interactive || controls_selftest;
#ifdef _WIN32
    if(!log_file && interactive) log_file = base / "CatArena.log";
#endif
    if(log_file && !cat::set_log_file(*log_file)) cat::log("warning", "app", "Cannot write the log file " + cat::path_utf8(*log_file));
#ifdef _WIN32
    // vulkan-1.dll is delay-loaded (CMakeLists.txt): check that the loader is there before the first Vulkan call.
    if(!SDL_LoadObject("vulkan-1.dll"))
        throw std::runtime_error("Vulkan is not available: vulkan-1.dll (the Vulkan loader, installed with the graphics driver) was not found. "
                                 "Install or update the GPU driver; Cat Arena needs Vulkan 1.2 or newer.");
#endif
    // The input self-test checks the input path (move, turn, jump, slide, crouch, fire, reload, swap). Without
    // enemies unless asked: a frag rerolls the weapon in hand with a full magazine, which makes R a no-op.
    if(input_selftest && !enemies_given) enemy_mode = EnemyMode::none;
    if(vk_info) {   // capability report only: no window, no assets
        cat::Renderer probe(64, 64, settings, shader_dir, vk_request);
        std::cout << vulkan_json(probe.report(), "") << '\n';
        return probe.validation_errors() ? 2 : 0;
    }
    std::optional<cat::scene::MapScene> scene;
    std::optional<cat::assets::Value> map_document;            // owns the compiled collision data
    std::optional<cat::physics::CollisionWorld> collision;
    std::optional<cat::nav::NavGraph> nav_graph;                // N6: route graph of the map (wave play only)
    std::filesystem::path root;
    // Loads render scene and collision world of one map. Used at start and for in-window map changes.
    // N7: in three parts, so that the loading screen can show its stages.
    auto load_render_scene = [&](const std::string& id) {
        const auto* entry = find_map(id);
        if(!entry) throw std::runtime_error("Unknown map '" + id + "' (use --list-maps)");
        map = id;
        nav_graph.reset();
        collision.reset();
        map_document.reset();
        cat::Stopwatch load;
        cat::scene::SceneOptions options;
        options.runtime_world_atlas = cat::scene::default_runtime_world_atlas(root);
        options.sky_box_dir = cat::scene::default_sky_box_dir(root);
        if(options.runtime_world_atlas.empty())
            cat::log("warning", "scene", "assets/n2/world-atlas-runtime.png not found: using the N1 atlas, which does not match the legacy runtime world textures");
        scene = cat::scene::load_map_scene(root, map, options);
        cat::log("info", "scene", "World atlas: " + scene->stats.world_atlas_source);
        if(scene->sky_box_missing) cat::log("warning", "scene", "Sky box '" + scene->sky_box_name + "' not found in assets/n4: the sky backdrop is the clear colour");
        else if(!scene->sky_box_name.empty()) cat::log("info", "scene", "Sky box: " + scene->sky_box_name + " (" + std::to_string(scene->sky_box.width) + " px faces)");
        const auto ms = std::chrono::duration<double, std::milli>(load.lap()).count();
        cat::log("info", "scene", std::string("Map: ") + entry->id + " (" + entry->wire_id + ") CAT ARENA: FERAL CLAW // " + entry->title);
        cat::log("info", "scene", "Loaded " + scene->id + " from " + cat::path_utf8(root) + " in " + std::to_string(static_cast<int>(ms)) + " ms (CPU preparation, not a GPU benchmark)");
    };
    auto load_collision = [&] {
        map_document = cat::assets::load_data(root / "maps" / map / "map.cadata");
        collision.emplace(map_document->at("compiled"));
        cat::log("info", "physics", "Collision world: " + std::to_string(collision->slabs().size()) + " slabs, " + std::to_string(collision->walls().size()) + " wall segments, " +
                                        std::to_string(collision->pads().size()) + " jump pads, " + std::to_string(collision->spawns().size()) + " spawns");
    };
    auto build_navigation = [&] {
        if(enemy_mode != EnemyMode::waves) return;
        cat::Stopwatch build;
        nav_graph.emplace(*collision);
        const auto layers = nav_graph->layer_counts();
        cat::log("info", "navigation", "Route graph: " + std::to_string(nav_graph->nodes().size()) + " nodes (" + std::to_string(layers[0]) + " / " + std::to_string(layers[1]) + " / " + std::to_string(layers[2]) + " per layer) in " +
                                           std::to_string(static_cast<int>(std::chrono::duration<double, std::milli>(build.lap()).count())) + " ms");
    };
    auto load_map = [&](const std::string& id) { load_render_scene(id); load_collision(); build_navigation(); };
    if(!triangle) {
        if(assets) root = *assets;
        else if(std::filesystem::exists(base / "assets/n1/catalog.cadata")) root = base / "assets/n1";
        else root = CAT_DEFAULT_ASSET_ROOT;
        if(root.empty() || !std::filesystem::exists(root / "catalog.cadata"))
            throw std::runtime_error("Baked assets not found (expected assets/n1 next to the executable or --assets <dir>)");
        if(!menus) load_map(std::string(map));   // N7: the menus load a map when one is chosen (loading screen)
        // The default capture pose is the Helios menu preview; other maps default to their first spawn.
        if(!menus && !camera_given && map != "helios-crucible" && !collision->spawns().empty()) {
            const auto& sp = collision->spawns()[0];
            pose = Pose{"spawn0", sp.x, sp.y, sp.z, sp.a, .05};
        }
    }
    // Rendered eye of the player: legacy q3ViewCamera() (bob, landing dip, step smoothing, roll) and
    // render(): horizontal FOV x moveFovScale.
    auto player_camera = [&](const cat::physics::PlayerController& pc, std::pair<std::uint32_t, std::uint32_t> e) {
        const auto v = pc.view_camera();
        auto c = cat::scene::legacy_camera(v.x, v.y, 0, pc.state().a, pc.state().pitch, fov * pc.fov_scale(), double(e.first) / double(e.second));
        c.position = {v.x, v.y, v.z}; c.forward = v.forward; c.right = v.right; c.up = v.up;
        if(third_person) for(std::size_t k = 0; k < 3; ++k) c.position[k] += -3.6 * v.forward[k] + .55 * v.up[k];   // debug chase view (no collision)
        return c;
    };
    // N5: spawn with weapons. Arena::spawn resets health, projectiles and pickups; the loadout is the
    // requested pair or a random pair (rollLoadout).
    auto spawn_session = [&](Session& s) {
        const auto& spawns = collision->spawns();
        if(spawn_index < 0 || static_cast<std::size_t>(spawn_index) >= spawns.size()) throw std::runtime_error("--spawn index out of range (map has " + std::to_string(spawns.size()) + " spawns)");
        const auto& sp = spawns[static_cast<std::size_t>(spawn_index)];
        if(player_pose) s.arena.spawn(player_pose->x, player_pose->y, player_pose->z, player_pose->yaw, player_pose->pitch, time);
        else s.arena.spawn(sp.x, sp.y, sp.z, sp.a, 0, time);
        s.show_hud = hud_enabled;
        if(start_weapons) s.arena.set_loadout(start_weapons->first, start_weapons->second); else s.arena.roll_loadout();
        s.practice = practice_targets && enemy_mode == EnemyMode::practice; s.map_title = find_map(map)->title;
        s.light_grid = light_grid; s.god = god; s.third_person = third_person;
        s.enemy_options.shadows = shadow_mode; s.enemy_options.sun_azimuth = scene->sun_azimuth;
        s.effects.clear(); s.enemy_fx.clear();
        s.place_targets();
        if(s.horde) {   // startWaves: the previous wave's total decides this wave's size (10, then x 1.25 rounded up)
            int total = 10;
            for(int w = 2; w < start_wave; ++w) total = static_cast<int>(std::ceil(total * 1.25));
            s.horde->set_wave(std::max(1, start_wave - 1), total);
            s.horde->begin_wave(start_wave);
            s.announce_wave();
            cat::log("info", "enemies", "Wave " + std::to_string(s.horde->wave_number()) + ": " + std::to_string(s.horde->wave_total()) + " enemies, health / speed / damage x " + std::to_string(s.horde->scale()) +
                                            ", " + std::to_string(s.arena.targets().size()) + " spawned at once");
        }
        const auto& w = cat::combat::weapons();
        cat::log("info", "physics", "Player at spawn " + std::to_string(spawn_index) + " '" + sp.tag + "' (" + std::to_string(sp.x) + ", " + std::to_string(sp.y) + ", " + std::to_string(sp.z) + ")");
        cat::log("info", "combat", std::string("Loadout: ") + w[static_cast<std::size_t>(s.arena.weapon(0))].name + " + " + w[static_cast<std::size_t>(s.arena.weapon(1))].name + (s.horde ? "; enemies " : "; practice targets ") +
                                       std::to_string(s.arena.targets().size()) + "; medkits " + std::to_string(s.arena.pickups().size()) + "; seed " + std::to_string(game_seed));
    };
    const auto overlay_dir = triangle ? std::filesystem::path{} : overlay_asset_dir(root);
    const auto enemy_dir = triangle ? std::filesystem::path{} : enemy_asset_dir(root);
    // Per map: the baked light volume the creatures are lit by (assets/n6/light-volume/<map>.png).
    auto attach_light_volume = [&](cat::Renderer& r) {
        if(enemy_dir.empty() || !r.enemies_loaded()) return;
        const auto file = enemy_dir / "light-volume" / (map + ".png");
        if(std::filesystem::exists(file)) r.set_light_volume(cat::scene::load_image(file));
        else { r.set_light_volume({}); cat::log("warning", "enemies", "No light volume for " + map + ": creatures get flat light"); }
    };
    auto attach_overlay = [&](cat::Renderer& r) {   // weapon models + effect atlas + creature meshes, once per renderer
        if(!overlay_enabled) return false;
        if(overlay_dir.empty()) { cat::log("warning", "overlay", "assets/n5 not found next to the map assets: weapon model, effects and HUD are not drawn"); return false; }
        cat::Stopwatch load;
        if(enemy_dir.empty()) cat::log("warning", "enemies", "assets/n6 not found next to the map assets: creatures are simulated but not drawn");
        else {
            r.load_enemies(load_enemy_assets(enemy_dir, root));
            attach_light_volume(r);
            cat::log("info", "enemies", "20 creature meshes, Marine Cat and light volume from " + cat::path_utf8(enemy_dir) + " in " + std::to_string(static_cast<int>(std::chrono::duration<double, std::milli>(load.lap()).count())) + " ms");
        }
        r.load_overlay(load_overlay_assets(overlay_dir));
        cat::log("info", "overlay", "Weapon models and effect atlas from " + cat::path_utf8(overlay_dir) + " in " + std::to_string(static_cast<int>(std::chrono::duration<double, std::milli>(load.lap()).count())) + " ms");
        return true;
    };
    auto camera_for = [&](const Pose& p, std::pair<std::uint32_t, std::uint32_t> e) {
        return cat::scene::legacy_camera(p.x, p.y, p.z, p.yaw, p.pitch, fov, double(e.first) / double(e.second));
    };

    // ---------------------------------------------------------------- offscreen captures (no SDL)
    if(offscreen) {
        if(!scene) throw std::runtime_error("--offscreen renders a map; remove --triangle");
        cat::Renderer renderer(offscreen->first, offscreen->second, settings, shader_dir, vk_request);
        renderer.load_scene(*scene);
        if(capture_dir) std::filesystem::create_directories(*capture_dir);
        if(combat_fixture) {   // replay one tape of the legacy combat fixture and write frames (comparison with legacy pictures)
            if(!capture_dir || scenario_name.empty() || capture_ticks.empty()) throw std::runtime_error("--combat-fixture needs --scenario, --capture-ticks and --capture-dir");
            std::ifstream in(*combat_fixture, std::ios::binary);
            std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::size_t at = 0;
            auto u32 = [&] { std::uint32_t v = 0; if(at + 4 > bytes.size()) throw std::runtime_error("combat fixture truncated"); std::memcpy(&v, bytes.data() + at, 4); at += 4; return v; };
            auto f64 = [&](std::size_t n) { std::vector<double> v(n); if(at + n * 8 > bytes.size()) throw std::runtime_error("combat fixture truncated"); if(n) std::memcpy(v.data(), bytes.data() + at, n * 8); at += n * 8; return v; };
            if(bytes.size() < 24 || std::string_view(bytes.data(), 8) != "CATCMBT1") throw std::runtime_error("Not a combat fixture: " + cat::path_utf8(*combat_fixture));
            at = 8;
            if(u32() != 1) throw std::runtime_error("Unsupported combat fixture version");
            const auto count = u32(); const auto tape_width = u32(), record_width = u32();
            if(tape_width != 3) throw std::runtime_error("Unexpected combat fixture layout");
            const bool overlay = attach_overlay(renderer);
            const double aspect = double(offscreen->first) / double(offscreen->second);
            for(std::uint32_t sc = 0; sc < count; ++sc) {
                const auto length = u32();
                if(at + length > bytes.size()) throw std::runtime_error("combat fixture truncated");
                const std::string name(bytes.data() + at, length); at += length;
                const auto ticks = u32();
                const auto start = f64(12);
                const auto targets = f64(static_cast<std::size_t>(start[10]) * 5), tape = f64(std::size_t(ticks) * 3);
                f64(std::size_t(ticks) * record_width);
                if(name != scenario_name) continue;
                Session session(*collision, static_cast<std::uint32_t>(static_cast<std::int32_t>(start[9])), fov);
                session.arena.spawn(start[0], start[1], start[2], start[3], start[4], start[5]);
                session.arena.set_health(start[6]);
                session.arena.set_loadout(static_cast<int>(start[7]), static_cast<int>(start[8]));
                for(std::size_t t = 0; t * 5 < targets.size(); ++t)
                    session.arena.add_target(targets[t * 5], targets[t * 5 + 1], targets[t * 5 + 2], static_cast<cat::combat::TargetClass>(static_cast<int>(targets[t * 5 + 3])), targets[t * 5 + 4]);
                session.practice = false; session.show_hud = hud_enabled; session.map_title = find_map(map)->title;
                std::size_t written = 0;
                for(std::uint32_t i = 0; i < ticks; ++i) {
                    session.step({static_cast<std::uint32_t>(tape[i * 3]), tape[i * 3 + 1], tape[i * 3 + 2]}, aspect);
                    // one effect build per tick, like the recording (the thunderbolt draws a random number per build)
                    cat::FrameView view{player_camera(session.arena.player(), *offscreen), session.arena.player().now(), features};
                    const auto built = session.build(*offscreen);
                    session.light(view);
                    if(std::find(capture_ticks.begin(), capture_ticks.end(), static_cast<double>(i)) == capture_ticks.end()) continue;
                    if(overlay) renderer.draw(view, built); else renderer.draw(view);
                    char file[160]; std::snprintf(file, sizeof file, "%s_%05u.png", name.c_str(), i);
                    cat::scene::write_png(*capture_dir / file, renderer.capture());
                    ++written;
                }
                cat::log("info", "combat", "Replayed " + name + " (" + std::to_string(ticks) + " ticks), wrote " + std::to_string(written) + " frames; " + session.json());
                return renderer.validation_errors() ? 2 : 0;
            }
            throw std::runtime_error("Scenario '" + scenario_name + "' is not in " + cat::path_utf8(*combat_fixture));
        }
        if(enemy_fixture) {   // replay one tape of a legacy enemy fixture and write frames
            if(!capture_dir || scenario_name.empty() || capture_ticks.empty()) throw std::runtime_error("--enemy-fixture needs --scenario, --capture-ticks and --capture-dir");
            if(!nav_graph) throw std::runtime_error("--enemy-fixture needs --enemies waves");
            std::ifstream in(*enemy_fixture, std::ios::binary);
            std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            std::size_t at = 0;
            auto u32 = [&] { std::uint32_t v = 0; if(at + 4 > bytes.size()) throw std::runtime_error("enemy fixture truncated"); std::memcpy(&v, bytes.data() + at, 4); at += 4; return v; };
            auto f64 = [&](std::size_t n) { std::vector<double> v(n); if(at + n * 8 > bytes.size()) throw std::runtime_error("enemy fixture truncated"); if(n) std::memcpy(v.data(), bytes.data() + at, n * 8); at += n * 8; return v; };
            if(bytes.size() < 24 || std::string_view(bytes.data(), 8) != "CATENMY1") throw std::runtime_error("Not an enemy fixture: " + cat::path_utf8(*enemy_fixture));
            at = 8;
            if(u32() != 1) throw std::runtime_error("Unsupported enemy fixture version");
            const auto count = u32(); const auto tape_width = u32(), record_width = u32();
            if(tape_width != 3) throw std::runtime_error("Unexpected enemy fixture layout");
            const bool overlay = attach_overlay(renderer);
            const double aspect = double(offscreen->first) / double(offscreen->second);
            for(std::uint32_t sc = 0; sc < count; ++sc) {
                const auto length = u32();
                if(at + length > bytes.size()) throw std::runtime_error("enemy fixture truncated");
                const std::string name(bytes.data() + at, length); at += length;
                const auto ticks = u32();
                const auto start = f64(16);
                const auto tape = f64(std::size_t(ticks) * 3);
                at += std::size_t(ticks) * record_width * 8;
                if(name != scenario_name) continue;
                const auto seed = static_cast<std::int32_t>(start[9]);
                Session session(*collision, static_cast<std::uint32_t>(seed), fov, &*nav_graph);
                session.arena.spawn(start[0], start[1], start[2], start[3], start[4], start[5]);
                session.arena.set_health(start[6]);
                session.arena.set_loadout(static_cast<int>(start[7]), static_cast<int>(start[8]));
                session.practice = false; session.show_hud = hud_enabled; session.map_title = find_map(map)->title;
                session.light_grid = light_grid; session.third_person = third_person; session.enemy_options.shadows = shadow_mode; session.enemy_options.sun_azimuth = scene->sun_azimuth;
                const int wave = static_cast<int>(start[10]);
                session.horde->set_wave(std::max(1, wave - 1), static_cast<int>(start[11]));
                session.horde->begin_wave(wave);
                std::size_t written = 0;
                for(std::uint32_t i = 0; i < ticks; ++i) {
                    session.step({static_cast<std::uint32_t>(tape[i * 3]), tape[i * 3 + 1], tape[i * 3 + 2]}, aspect);
                    // the recording poses every enemy once per tick (enemyEvalPose after update), so does the replay
                    for(const auto& b : session.arena.targets()) session.enemy_fx.pose(b, session.arena);
                    (void)session.enemy_fx.marine_pose(session.arena);   // ... and the local player as a Marine Cat (same animation stream)
                    // ... and follows the first-person weapon every tick (WFX.viewmodelOffsets: the raise after a weapon change);
                    // the effects themselves are built at the picture ticks only, as in the recording
                    (void)session.effects.viewmodel_offsets(session.arena.current_weapon(), session.arena.reloading());
                    if(std::find(capture_ticks.begin(), capture_ticks.end(), static_cast<double>(i)) == capture_ticks.end()) continue;
                    cat::FrameView view{player_camera(session.arena.player(), *offscreen), session.arena.player().now(), features};
                    // the legacy tool takes its pictures with Math.random = () => 0.5 (the thunderbolt's jitter, ...)
                    session.effects.random_stream().set_constant(.5); session.enemy_fx.random_stream().set_constant(.5);
                    const auto built = session.build(*offscreen);
                    session.effects.random_stream().clear_constant(); session.enemy_fx.random_stream().clear_constant();
                    session.light(view);
                    if(overlay) renderer.draw(view, built); else renderer.draw(view);
                    char file[160]; std::snprintf(file, sizeof file, "%s_%05u.png", name.c_str(), i);
                    cat::scene::write_png(*capture_dir / file, renderer.capture());
                    ++written;
                    const auto st = renderer.stats();
                    cat::log("info", "enemies", std::string(file) + ": " + std::to_string(st.enemy_instances) + " creature instances in " + std::to_string(st.enemy_draws) + " draws, " + std::to_string(st.effect_quads) + " effect quads, " +
                                                    std::to_string(session.arena.targets().size()) + " enemies alive");
                }
                cat::log("info", "enemies", "Replayed " + name + " (" + std::to_string(ticks) + " ticks), wrote " + std::to_string(written) + " frames; " + session.json());
                return renderer.validation_errors() ? 2 : 0;
            }
            throw std::runtime_error("Scenario '" + scenario_name + "' is not in " + cat::path_utf8(*enemy_fixture));
        }
        if(enemy_lineup) {   // one staged scene of tools/n6/legacy_enemies.cjs --lineup-dir (comparison with its picture)
            if(!capture || lineup_shot.empty()) throw std::runtime_error("--enemy-lineup needs --lineup-shot and --capture");
            if(!nav_graph) throw std::runtime_error("--enemy-lineup needs --enemies waves");
            std::ifstream in(*enemy_lineup);
            if(!in) throw std::runtime_error("Cannot open " + cat::path_utf8(*enemy_lineup));
            const bool overlay = attach_overlay(renderer);
            Session session(*collision, 1, fov, &*nav_graph);
            session.arena.random_stream().set_constant(.5); session.effects.random_stream().set_constant(.5); session.enemy_fx.random_stream().set_constant(.5);
            session.practice = false; session.show_hud = hud_enabled; session.map_title = find_map(map)->title;
            session.light_grid = light_grid; session.third_person = third_person; session.enemy_options.sun_azimuth = scene->sun_azimuth;
            std::string line; bool inside = false, found = false; double now = 0;
            std::array<double, 3> player_xyz{};
            while(std::getline(in, line)) {
                std::istringstream ls(line); std::string kind; ls >> kind;
                if(kind == "shot") {
                    std::string name; int shadows = 1, total = 0, kills = 0; ls >> name >> shadows >> total >> kills >> now;
                    inside = name == lineup_shot; if(!inside) continue;
                    found = true; session.enemy_options.shadows = shadows; session.horde->stage_wave(1, total, total, kills);
                } else if(!inside) continue;
                else if(kind == "player") {
                    double x, y, z, yaw, pitch; ls >> x >> y >> z >> yaw >> pitch;
                    session.arena.spawn(x, y, z, yaw, pitch, now); session.arena.set_loadout(0, 1); player_xyz = {x, y, z};
                } else if(kind == "bot") {
                    int type; double x, y, z, a, hit, total, wind, recover, recover_total; ls >> type >> x >> y >> z >> a >> hit >> total >> wind >> recover >> recover_total;
                    auto& b = session.horde->stage_enemy(type, x, y, z, a);
                    b.hit_flash = hit; b.attack_total = total; b.attack_wind = wind; b.attack_recover = recover; b.attack_recover_total = recover_total;
                } else if(kind == "corpse") {
                    int type, variant; double x, y, z, a, hit, t, side, flash, floor_z, z_now; ls >> type >> x >> y >> z >> a >> hit >> t >> variant >> side >> flash >> floor_z >> z_now;
                    const auto id = session.horde->stage_corpse(type, x, y, z_now, floor_z, a, t);
                    cat::fx::PoseInput at; at.x = x; at.y = y; at.z = z; at.a = a; at.hit_flash = hit; at.has_target = true; at.tx = player_xyz[0]; at.ty = player_xyz[1]; at.tz = player_xyz[2];
                    session.enemy_fx.stage_body(id, type, at, now, variant, side, flash);
                } else if(kind == "guest") {
                    cat::fx::Guest g; g.id = static_cast<std::uint32_t>(session.enemy_options.guests.size() + 1); ls >> g.x >> g.y >> g.z >> g.a >> g.vx >> g.vy >> g.color[0] >> g.color[1] >> g.color[2];
                    session.enemy_options.guests.push_back(g);
                } else if(kind == "end") break;
            }
            if(!found) throw std::runtime_error("Shot '" + lineup_shot + "' is not in " + cat::path_utf8(*enemy_lineup));
            cat::FrameView view{player_camera(session.arena.player(), *offscreen), session.arena.player().now(), features};
            const auto built = session.build(*offscreen);
            session.light(view);
            if(overlay) renderer.draw(view, built); else renderer.draw(view);
            cat::scene::write_png(*capture, renderer.capture());
            const auto st = renderer.stats();
            cat::log("info", "enemies", "Lineup '" + lineup_shot + "': " + std::to_string(session.arena.targets().size()) + " enemies, " + std::to_string(session.horde->corpses().size()) + " bodies, " +
                                            std::to_string(session.enemy_options.guests.size()) + " Marine Cats; " + std::to_string(st.enemy_instances) + " creature instances in " + std::to_string(st.enemy_draws) +
                                            " draws, " + std::to_string(st.effect_quads) + " effect quads -> " + cat::path_utf8(*capture));
            return renderer.validation_errors() ? 2 : 0;
        }
        if(ui_capture) {   // N7: every menu page, the settings tabs and the in-game HUD (comparison with docs/n7-evidence/legacy-ui)
            const auto n7 = n7_asset_dir(root);
            if(n7.empty()) throw std::runtime_error("--ui-capture needs assets/n7 next to the map assets");
            std::filesystem::create_directories(*ui_capture);
            UiKit kit = load_ui_kit(n7);
            renderer.load_ui(kit.atlas.image());
            const bool overlay = attach_overlay(renderer);
            cat::GameSettings gs;
            gs.map = map;
            cat::ui::Menu menu(gs, menu_maps());
            menu.select_map(map);
            const double W = offscreen->first, H = offscreen->second, k = ui_scale(W, H, 1);
            const std::pair<std::uint32_t, std::uint32_t> size = *offscreen;
            std::optional<Session> session;
            std::ostringstream index;
            // N8 pages: a staged server list (sample rows for the picture, not a live relay)
            cat::ui::MultiplayerView mp;
            mp.list_status = "3 LIVE SERVERS · 7 PLAYERS ONLINE";
            mp.rooms = {{"K7PQ2M", "Ashs Server", "Ash", "REACTOR BASTION", 2, 4, true, true}, {"WX3RT9", "Night Shift", "Brick", "THE ARSENAL", 1, 4, true, true},
                        {"HB4NZE", "Full House", "Cinder", "BASTION KEEP", 4, 4, false, true}};
            auto frame_of = [&](cat::ui::Canvas& c, bool in_game, const std::vector<cat::ui::HudElement>& hud) {
                cat::ui::MenuFrame f; f.time = 1; f.scale = k; f.safe = {0, 0, W, H}; f.in_game = in_game; f.current_map = map; f.background = kit.background; f.touch = force_touch; f.hud = hud; f.mp = &mp;
                f.platform = "DESKTOP";
                cat::ui::Canvas scratch(&kit.atlas, W, H);
                menu.build(scratch, f); menu.build(c, f);   // the second build draws the settled focus
            };
            auto shot = [&](const std::string& name, const cat::FrameView* view, cat::Overlay o, const cat::ui::Canvas& c) {
                attach_ui(o, c);
                if(view) renderer.draw(*view, o); else renderer.draw_ui(o);
                const auto file = *ui_capture / (name + ".png");
                cat::scene::write_png(file, renderer.capture());
                const auto st = renderer.stats();
                index << name << ".png\t" << st.ui_triangles << " UI triangles\t" << st.draw_calls << " draws\n";
                cat::log("info", "ui", "Wrote " + cat::path_utf8(file) + " (" + std::to_string(st.ui_triangles) + " UI triangles)");
            };
            {   // pages without the world
                cat::ui::Canvas c(&kit.atlas, W, H);
                menu.show_home(); frame_of(c, false, {}); shot("home", nullptr, {}, c);
                menu.handle({cat::ui::MenuEvent::Kind::key, cat::ui::NavKey::back}); c.clear(); frame_of(c, false, {}); shot("exit_dialog", nullptr, {}, c);
                static const char* const tabs[] = {"view", "graphics", "performance", "input", "audio", "hud"};
                menu.show_settings(false);
                for(int t = 0; t < 6; ++t) {
                    while(menu.settings_tab() != t) menu.handle({cat::ui::MenuEvent::Kind::key, cat::ui::NavKey::next_tab});
                    c.clear(); frame_of(c, false, {}); shot(std::string("settings_") + tabs[t], nullptr, {}, c);
                }
                cat::ui::LoadingState ls; ls.map_title = find_map(map)->title; ls.phase = 7; ls.ready = true; ls.info = "NATIVE LOAD · VULKAN";
                menu.show_loading(ls); c.clear(); frame_of(c, false, {}); shot("loading_ready", nullptr, {}, c);
                ls.phase = 3; ls.ready = false; menu.show_loading(ls); c.clear(); frame_of(c, false, {}); shot("loading_phase3", nullptr, {}, c);
                menu.show_multiplayer(); c.clear(); frame_of(c, false, {}); shot("multiplayer", nullptr, {}, c);
                menu.show_servers(); c.clear(); frame_of(c, false, {}); shot("servers", nullptr, {}, c);
                mp.status = "Joining selected server..."; menu.show_connecting(); c.clear(); frame_of(c, false, {}); shot("connecting", nullptr, {}, c);
                mp.status.clear();
            }
            {   // map selection over the live view of the map (preview camera)
                const auto p = preview_pose(map);
                cat::FrameView view{camera_for(p, size), time, features};
                cat::ui::Canvas c(&kit.atlas, W, H);
                menu.show_maps(); frame_of(c, false, {});
                cat::Overlay o; o.world = false;
                shot("maps", &view, o, c);
                menu.show_host_maps(); c.clear(); frame_of(c, false, {});
                shot("maps_host", &view, o, c);
            }
            if(overlay) {   // in-game HUD after two seconds of play, keyboard / mouse and touch layouts, pause, HUD editor
                session.emplace(*collision, game_seed, fov, nav_graph ? &*nav_graph : nullptr);
                spawn_session(*session);
                session->show_hud = false;
                const double aspect = W / H;
                for(int t = 0; t < 240; ++t) session->step({}, aspect);
                cat::RenderStats st0{}; st0.tracked_bytes = renderer.stats().tracked_bytes;
                cat::PerfMeter::Summary perf; perf.fps_live = 0;
                for(const bool touch : {false, true}) {
                    cat::FrameView view{player_camera(session->arena.player(), size), session->arena.player().now(), features};
                    auto o = session->build(size); session->light(view);
                    cat::ui::Canvas c(&kit.atlas, W, H);
                    cat::ui::HudView hv; hv.settings = &gs; hv.scale = k; hv.safe = {0, 0, W, H}; hv.touch = touch || force_touch; hv.captured = true;
                    hv.perf = perf_lines(gs, perf, st0, renderer.timestamps_supported());
                    const auto hud = cat::ui::build_hud(c, hud_info(*session), hv);
                    shot(touch ? "hud_touch" : "hud_kbm", &view, o, c);
                    if(touch) continue;
                    menu.show_pause(); frame_of(c, true, hud); shot("pause", &view, o, c);
                    c.clear(); cat::ui::build_hud(c, hud_info(*session), hv); menu.show_settings(true); frame_of(c, true, hud); shot("settings_from_pause", &view, o, c);
                    hv.editing = true; hv.selected = menu.hud_selected(); c.clear(); const auto edit = cat::ui::build_hud(c, hud_info(*session), hv); menu.show_hud_editor(true); frame_of(c, true, edit); shot("hud_editor", &view, o, c);
                }
                {   // N8: three staged teammates in front of the player (Marine Cats, name tags, a downed one), revive line, room badge
                    const auto& pl = session->arena.player().state();
                    const double ca = std::cos(pl.a), sa = std::sin(pl.a);
                    struct Mate { const char* name; double d, side; bool downed; double hp; };
                    const Mate mates[] = {{"Ash", 4.5, .9, false, 82}, {"Brick", 3.2, -.8, true, 0}, {"Cinder", 9, .2, false, 41}};
                    cat::ui::CoopHud ch; ch.badge = "HOSTING · 4/4 PLAYERS"; ch.revive = "HOLD F TO REVIVE Brick · 4 SECONDS";
                    session->enemy_options.guests.clear();
                    std::uint32_t id = 1;
                    for(const auto& m : mates) {
                        double x = pl.x + ca * m.d - sa * m.side, y = pl.y + sa * m.d + ca * m.side, z = pl.z;
                        if(const auto s = collision->support(x, y, pl.z + .5, .6)) z = s->z;
                        cat::fx::Guest g; g.id = id++; g.x = x; g.y = y; g.z = z; g.a = pl.a + 3.14159; g.downed = m.downed; g.color = {.81F, .89F, 1.F};
                        session->enemy_options.guests.push_back(g);
                    }
                    cat::FrameView view{player_camera(session->arena.player(), size), session->arena.player().now(), features};
                    auto o = session->build(size); session->light(view);
                    std::size_t i = 0;
                    for(const auto& g : session->enemy_options.guests) {
                        const auto& m = mates[i++];
                        if(auto t = project_tag(session->frame.camera, *collision, g.x, g.y, g.z, g.downed, W, H)) { t->name = m.name; t->hp = m.hp; t->host = false; ch.tags.push_back(*t); }
                    }
                    cat::ui::Canvas c(&kit.atlas, W, H);
                    cat::ui::HudView hv; hv.settings = &gs; hv.scale = k; hv.safe = {0, 0, W, H}; hv.touch = force_touch; hv.captured = true;
                    const auto hud = cat::ui::build_hud(c, hud_info(*session), hv);
                    cat::ui::build_coop_hud(c, ch, hv);
                    shot("coop_hud", &view, o, c);
                    cat::log("info", "ui", "Co-op HUD: " + std::to_string(ch.tags.size()) + " name tags, " + std::to_string(session->enemy_frame.marines.size()) + " Marine Cat instances");
                    mp.in_room = true; mp.hosting = true; mp.footer = "CO-OP · HOSTING · 4/4";
                    menu.show_pause(); frame_of(c, true, hud); shot("pause_coop", &view, o, c);
                    mp.in_room = false;
                }
            }
            std::ofstream(*ui_capture / "index.tsv") << index.str();
            return renderer.validation_errors() ? 2 : 0;
        }
        if(walk) {   // headless first-person traversal: fixed 120 Hz ticks, a frame every --capture-every ticks
            if(!capture_dir) throw std::runtime_error("--walk needs --capture-dir");
            const auto tape = parse_walk(*walk);
            const bool overlay = attach_overlay(renderer);
            Session session(*collision, game_seed, fov, nav_graph ? &*nav_graph : nullptr);
            spawn_session(session);
            auto& pc = session.arena.player();
            const double aspect = double(offscreen->first) / double(offscreen->second);
            std::ostringstream trace;
            trace << "{\n  \"map\": \"" << scene->id << "\", \"spawn\": " << spawn_index << ", \"ticks\": " << tape.size() << ", \"tickSeconds\": \"1/120\", \"script\": \"" << json_escape(*walk)
                  << "\",\n  \"frames\": [\n";
            bool first = true;
            for(std::size_t i = 0; i <= tape.size(); ++i) {
                if(i % static_cast<std::size_t>(capture_every) == 0 || i == tape.size()) {
                    cat::FrameView view{player_camera(pc, *offscreen), pc.now(), features};
                    if(overlay) { const auto built = session.build(*offscreen); session.light(view); renderer.draw(view, built); } else renderer.draw(view);
                    char name[32]; std::snprintf(name, sizeof name, "walk_%05zu.png", i);
                    cat::scene::write_png(*capture_dir / name, renderer.capture());
                    const auto& p = pc.state(); const auto cam = pc.view_camera();
                    trace << (first ? "" : ",\n") << "    {\"tick\": " << i << ", \"file\": \"" << name << "\", \"feet\": [" << p.x << ", " << p.y << ", " << p.z << "], \"velocity\": ["
                          << p.vx << ", " << p.vy << ", " << p.vz << "], \"yaw\": " << p.a << ", \"pitch\": " << p.pitch << ", \"ground\": " << (p.ground ? "true" : "false")
                          << ", \"stance\": \"" << stance_name(pc.stance()) << "\", \"eyeZ\": " << cam.z << ", \"roll\": " << cam.roll << ", \"fovScale\": " << pc.fov_scale()
                          << ", \"colliderHeight\": " << pc.height() << "}";
                    first = false;
                }
                if(i < tape.size()) session.step(tape[i], aspect);
            }
            const auto& c = pc.contacts();
            trace << "\n  ],\n  \"contacts\": {\"horizontal\": " << c.horizontal << ", \"ceiling\": " << c.ceiling << ", \"landings\": " << c.landings << ", \"steps\": " << c.steps
                  << ", \"recoveries\": " << c.recoveries << "}, \"jumps\": " << pc.jumps() << ", \"padLaunches\": " << pc.pad_launches() << ", \"footsteps\": " << pc.view().footsteps
                  << ",\n  \"combat\": " << session.json() << ", \"overlay\": " << (overlay ? "true" : "false")
                  << ",\n  \"validationWarnings\": " << renderer.validation_warnings() << ", \"validationErrors\": " << renderer.validation_errors() << "\n}\n";
            std::ofstream out(report ? *report : *capture_dir / "walk.json");
            out << trace.str();
            cat::log("info", "physics", "Walk finished after " + std::to_string(tape.size()) + " ticks at (" + std::to_string(pc.state().x) + ", " + std::to_string(pc.state().y) + ", " +
                                            std::to_string(pc.state().z) + ")");
            return renderer.validation_errors() ? 2 : 0;
        }
        std::vector<Pose> poses = capture_set ? read_poses(*capture_set) : std::vector<Pose>{pose};
        std::ostringstream json;
        cat::log("info", "vulkan", "Scene objects: " + std::to_string(renderer.report().material_pipelines) + " material pipelines, " +
                                       std::to_string(renderer.report().descriptor_sets) + " descriptor sets");
        const auto dev = renderer.report();
        json << "{\n  \"map\": \"" << scene->id << "\",\n  \"device\": \"" << json_escape(dev.device) << "\",\n  \"deviceType\": \"" << dev.device_type
             << "\",\n  \"api\": \"" << dev.api << "\",\n  \"loaderApi\": \"" << dev.loader_api << "\",\n  \"colorFormat\": \"" << dev.color_format
             << "\",\n  \"depthFormat\": \"" << dev.depth_format << "\",\n  \"anisotropy\": " << (dev.anisotropy ? dev.max_anisotropy : 0)
             << ",\n  \"validationLayer\": " << (dev.validation ? "true" : "false") << ",\n  \"pipelines\": " << dev.pipelines
             << ",\n  \"vulkan\": " << vulkan_json(dev, "  ")
             << ",\n  \"extent\": [" << offscreen->first << ", " << offscreen->second << "],\n  \"fov\": " << fov << ",\n  \"time\": " << time << ",\n  \"frames\": [\n";
        for(std::size_t i = 0; i < poses.size(); ++i) {
            const auto& p = poses[i];
            cat::FrameView view{camera_for(p, *offscreen), time, features};
            for(int w = 0; w < std::max(1, warmup); ++w) renderer.draw(view);
            const auto st = renderer.stats();
            const auto image = renderer.capture();
            const auto out = capture_set ? *capture_dir / (p.name + ".png") : *capture;
            cat::scene::write_png(out, image);
            std::uint64_t sum = 0, sky = 0;
            for(std::size_t k = 0; k < image.rgba.size(); k += 4) {
                sum += image.rgba[k] + image.rgba[k + 1] + image.rgba[k + 2];
                const auto r = image.rgba[k], g = image.rgba[k + 1], b = image.rgba[k + 2];
                if(std::abs(r - int(std::lround(scene->theme.sky[0] * 255))) <= 1 && std::abs(g - int(std::lround(scene->theme.sky[1] * 255))) <= 1 &&
                   std::abs(b - int(std::lround(scene->theme.sky[2] * 255))) <= 1) ++sky;
            }
            const double pixels = double(image.width) * image.height;
            json << "    {\"name\": \"" << p.name << "\", \"file\": \"" << json_escape(cat::path_utf8(out.filename())) << "\", \"pose\": [" << p.x << ", " << p.y
                 << ", " << p.z << ", " << p.yaw << ", " << p.pitch << "], \"drawCalls\": " << st.draw_calls << ", \"materialPasses\": " << st.material_passes
                 << ", \"visibleClusters\": " << st.visible_clusters << ", \"culledClusters\": " << st.culled_clusters << ", \"meanLuma\": "
                 << std::fixed << std::setprecision(2) << double(sum) / (pixels * 3) << ", \"clearColorFraction\": " << std::setprecision(4) << double(sky) / pixels
                 << std::defaultfloat << "}" << (i + 1 < poses.size() ? "," : "") << "\n";
            cat::log("info", "capture", "Wrote " + cat::path_utf8(out) + " draws=" + std::to_string(st.draw_calls) + " passes=" + std::to_string(st.material_passes) +
                                            " visibleClusters=" + std::to_string(st.visible_clusters));
        }
        json << "  ],\n  \"validationWarnings\": " << renderer.validation_warnings() << ",\n  \"validationErrors\": " << renderer.validation_errors() << "\n}\n";
        if(report) { std::ofstream r(*report); r << json.str(); if(!r) throw std::runtime_error("Cannot write report"); }
        if(renderer.validation_errors()) { cat::log("error", "vulkan", "Validation errors reported"); return 2; }
        return 0;
    }

    // ---------------------------------------------------------------- interactive window
    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) throw std::runtime_error(SDL_GetError());
    cat::ScopeExit quit([] { SDL_Quit(); });
    if(menus) SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");   // N7: fingers are handled as touches (menus, touch controls), not as a mouse
    SDL_Window* window = SDL_CreateWindow(menus ? "Cat Arena: Feral Claw" : scene ? "Cat Arena: Feral Claw - N6 enemies" : "Cat Arena: Feral Claw - N0",
                                          settings.width, settings.height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if(!window) throw std::runtime_error(SDL_GetError());
    cat::ScopeExit destroy([&] { SDL_DestroyWindow(window); });
    cat::Renderer renderer(window, settings, shader_dir, vk_request);
    if(scene) renderer.load_scene(*scene);
    const bool overlay = scene && !fly && attach_overlay(renderer);
    cat::FixedClock simulation; cat::Stopwatch timer; cat::Input input; bool running = true, captured = false; int rendered = 0;
    double sim_time = time;
    const bool walking = scene && !fly;
    std::optional<Session> session;                      // N5: the player lives inside the weapon simulation
    cat::physics::PlayerController* player = nullptr;
    std::uint32_t player_keys = 0;
    if(!seed_given && !input_selftest) game_seed = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());   // interactive play: a new loadout every run
    auto start_session = [&] { session.emplace(*collision, game_seed, fov, nav_graph ? &*nav_graph : nullptr); spawn_session(*session); player = &session->arena.player(); if(!seed_given) ++game_seed; };
    // Keys pressed and released between two simulation ticks (a quick tap at a low frame rate) still
    // count for one tick, like the legacy keydown / mousedown handlers that act on the event itself.
    std::uint32_t tapped_keys = 0;
    bool played = false;   // N6.5: the pause panel says "PAUSED" once this game has run
    if(menus) {
#include "main_menus.inc"
    }
    if(walking) start_session();
    cat::log("info", "app", walking ? "N6: click to capture the mouse; WASD move, Shift sprint, Space jump, Ctrl crouch, C slide; left mouse fires, R reloads, Q/Tab swaps weapons; 1-4 change map, F5 restarts, F11 fullscreen; Esc releases the mouse (pauses a game started by hand), Esc again quits. Enemies come in waves (--enemies practice|none for the N5 targets or an empty map)."
                            : scene ? "Fly camera (--fly): click to capture the mouse, WASD/Space/Ctrl fly, Shift fast, Esc release/quit. No collision."
                                    : "N0 triangle shell; 120Hz core. Gameplay not implemented.");
    // --controls-selftest (N6.5): click to play, Esc pauses (game time must stop), click resumes, F11 toggles
    // fullscreen twice, F5 restarts the map (wave 1, fresh clock), Esc releases and Esc again quits.
    struct { int stage = 0; double paused_at = 0, resumed_at = 0; bool paused_ok = false, resumed_ok = false, fullscreen_seen = false, windowed_again = false, restarted_ok = false, quit_by_esc = false; } controls;
    auto controls_step = [&](std::uint64_t tick) {
        auto key = [&](SDL_Scancode code) { for(bool down : {true, false}) { SDL_Event e{}; e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP; e.key.windowID = SDL_GetWindowID(window); e.key.scancode = code; e.key.down = down; SDL_PushEvent(&e); } };
        auto click = [&] { SDL_Event e{}; e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.windowID = SDL_GetWindowID(window); e.button.button = SDL_BUTTON_LEFT; e.button.down = true; SDL_PushEvent(&e);
                           e.type = SDL_EVENT_MOUSE_BUTTON_UP; e.button.down = false; SDL_PushEvent(&e); };
        auto& c = controls;
        const double now = session ? session->arena.now() : 0;
        if(c.stage == 0 && tick >= 5) { click(); c.stage = 1; }
        else if(c.stage == 1 && tick >= 60) { c.paused_at = now; key(SDL_SCANCODE_ESCAPE); c.stage = 2; }
        else if(c.stage == 2 && tick >= 70) { c.paused_at = now; c.stage = 3; }                                   // pause has taken effect
        else if(c.stage == 3 && tick >= 130) { c.paused_ok = !captured && now == c.paused_at; click(); c.stage = 4; }
        else if(c.stage == 4 && tick >= 190) { c.resumed_ok = captured && now > c.paused_at + .3; key(SDL_SCANCODE_F11); c.stage = 5; }
        else if(c.stage == 5 && tick >= 250) { SDL_SyncWindow(window); c.fullscreen_seen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0; key(SDL_SCANCODE_F11); c.stage = 6; }
        else if(c.stage == 6 && tick >= 310) { SDL_SyncWindow(window); c.windowed_again = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0; c.resumed_at = now; key(SDL_SCANCODE_F5); c.stage = 7; }
        else if(c.stage == 7 && tick >= 320) { c.restarted_ok = session && session->horde && session->horde->wave_number() == 1 && session->arena.frags() == 0 && now < c.resumed_at; c.stage = 8; }
        else if(c.stage == 8 && tick >= 360) { key(SDL_SCANCODE_ESCAPE); c.stage = 9; }
        else if(c.stage == 9 && tick >= 380) { key(SDL_SCANCODE_ESCAPE); c.stage = 10; c.quit_by_esc = true; }
    };
    // --input-selftest: events are pushed into SDL's queue and then handled by the same code as real input.
    const double selftest_yaw0 = player ? player->state().a : 0, selftest_x0 = player ? player->state().x : 0, selftest_y0 = player ? player->state().y : 0;
    int selftest_motions = 0; bool selftest_slid = false, selftest_crouched = false, selftest_captured = false;
    auto push_key = [&](SDL_Scancode code, bool down) {
        SDL_Event e{}; e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP; e.key.windowID = SDL_GetWindowID(window); e.key.scancode = code; e.key.down = down; e.key.repeat = false;
        SDL_PushEvent(&e);
    };
    auto inject = [&](std::uint64_t tick) {
        static std::uint64_t done = 0;   // each step once, in order, as simulation time passes
        auto at = [&](std::uint64_t when, std::uint64_t id) { if(tick >= when && done < id) { done = id; return true; } return false; };
        if(at(5, 1)) { SDL_Event e{}; e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.windowID = SDL_GetWindowID(window); e.button.button = SDL_BUTTON_LEFT; e.button.down = true; SDL_PushEvent(&e); }
        if(at(10, 2)) push_key(SDL_SCANCODE_W, true);
        if(tick >= 30 && selftest_motions < 40 && captured) { SDL_Event e{}; e.type = SDL_EVENT_MOUSE_MOTION; e.motion.windowID = SDL_GetWindowID(window); e.motion.xrel = 5; e.motion.yrel = 1; SDL_PushEvent(&e); ++selftest_motions; }
        if(at(120, 3)) push_key(SDL_SCANCODE_LSHIFT, true);
        if(at(240, 4)) push_key(SDL_SCANCODE_SPACE, true);
        if(at(246, 5)) push_key(SDL_SCANCODE_SPACE, false);
        if(at(400, 6)) push_key(SDL_SCANCODE_C, true);
        if(at(404, 7)) push_key(SDL_SCANCODE_C, false);
        if(at(600, 8)) { push_key(SDL_SCANCODE_LSHIFT, false); push_key(SDL_SCANCODE_RCTRL, true); }
        if(at(720, 9)) { push_key(SDL_SCANCODE_RCTRL, false); push_key(SDL_SCANCODE_W, false); }
        // N5: hold the fire button, release, reload, swap weapons
        auto push_button = [&](bool down) { SDL_Event e{}; e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP; e.button.windowID = SDL_GetWindowID(window); e.button.button = SDL_BUTTON_LEFT; e.button.down = down; SDL_PushEvent(&e); };
        if(at(728, 10)) push_button(true);
        if(at(742, 11)) push_button(false);
        if(at(748, 12)) push_key(SDL_SCANCODE_R, true);
        if(at(750, 13)) push_key(SDL_SCANCODE_R, false);
        if(at(760, 14)) push_key(SDL_SCANCODE_Q, true);
        if(at(762, 15)) push_key(SDL_SCANCODE_Q, false);
    };
    std::optional<std::string> pending_map;
    int map_switches = 0;
    while(running) {
        auto start = std::chrono::steady_clock::now();
        if(map_cycle_test && scene && frames >= 8 && rendered > 0 && rendered % (frames / 4) == 0 && map_switches < 3 && !pending_map)
            pending_map = kMaps[(static_cast<std::size_t>(find_map(map) - kMaps) + 1) % 4].id;
        if(pending_map) {   // in-window map change: new scene, new collision world, player at the new map's spawn
            const std::string next = *pending_map;
            pending_map.reset();
            if(next != map) {
                player = nullptr; session.reset();
                load_map(next);
                renderer.load_scene(*scene);
                attach_light_volume(renderer);
                if(walking) { spawn_index = 0; start_session(); player_keys = 0; played = false; }
                else if(!collision->spawns().empty()) { const auto& sp = collision->spawns()[0]; pose = Pose{"spawn0", sp.x, sp.y, sp.z, sp.a, .05}; }
                SDL_SetWindowTitle(window, (std::string("Cat Arena: Feral Claw - ") + find_map(map)->title).c_str());
                ++map_switches;
                timer.lap();   // loading time is not simulation time
            }
        }
        if(input_selftest && walking) inject(simulation.ticks());
        if(controls_selftest && walking) controls_step(simulation.ticks());
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            if(event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
            if(event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_RESTORED) renderer.resize();
            if(event.type == SDL_EVENT_WINDOW_FOCUS_LOST) { input.release_all(); player_keys = 0; tapped_keys = 0; captured = false; SDL_SetWindowRelativeMouseMode(window, false); }
            if(scene && event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.scancode >= SDL_SCANCODE_1 && event.key.scancode <= SDL_SCANCODE_4)
                pending_map = kMaps[event.key.scancode - SDL_SCANCODE_1].id;
            if(walking && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) && !event.key.repeat) {
                // The legacy key codes read by the movement code (KeyW/S/A/D, Space, Control*, Shift*, KeyC), by physical key.
                using namespace cat::physics;
                std::uint32_t bit = 0;
                switch(event.key.scancode) {
                    case SDL_SCANCODE_W: bit = key_forward; break; case SDL_SCANCODE_S: bit = key_back; break;
                    case SDL_SCANCODE_A: bit = key_left; break; case SDL_SCANCODE_D: bit = key_right; break;
                    case SDL_SCANCODE_SPACE: bit = key_jump; break; case SDL_SCANCODE_C: bit = key_slide; break;
                    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: bit = key_crouch; break;
                    case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: bit = key_sprint; break;
                    case SDL_SCANCODE_R: bit = cat::combat::key_reload; break;                                   // legacy KeyR
                    case SDL_SCANCODE_Q: case SDL_SCANCODE_TAB: bit = cat::combat::key_switch; break;            // legacy KeyQ / Tab
                    default: break;
                }
                if(event.type == SDL_EVENT_KEY_DOWN) { player_keys |= bit; tapped_keys |= bit; } else player_keys &= ~bit;
            }
            if(event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE && captured) {
                captured = false; SDL_SetWindowRelativeMouseMode(window, false); continue;   // first Esc releases the mouse (and pauses an interactive game)
            }
            if(event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.scancode == SDL_SCANCODE_F11) {   // N6.5: borderless fullscreen <-> window
                SDL_SetWindowFullscreen(window, (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) == 0);
                continue;
            }
            if(event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.scancode == SDL_SCANCODE_F5 && walking) {   // N6.5: restart the map at wave 1
 start_session(); player_keys = 0; tapped_keys = 0; played = false;
                cat::log("info", "app", "Restarted " + std::string(map) + " (F5)");
                continue;
            }
            if(event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) if(auto mapped = key(event.key.scancode)) input.set(*mapped, event.type == SDL_EVENT_KEY_DOWN);
            if(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT && scene && !captured) {
                captured = SDL_SetWindowRelativeMouseMode(window, true); continue;
            }
            if((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) && event.button.button == SDL_BUTTON_LEFT) {
                input.set(cat::Key::mouse_left, event.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                // legacy mousedown / mouseup: fire while the button is held (only with the mouse captured)
                if(walking) { if(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && captured) { player_keys |= cat::combat::key_fire; tapped_keys |= cat::combat::key_fire; } else player_keys &= ~static_cast<std::uint32_t>(cat::combat::key_fire); }
            }
            if(event.type == SDL_EVENT_MOUSE_MOTION && captured) {
                // Legacy basis: right = (-sin a, cos a), so turning toward screen-right increases yaw;
                // positive pitch looks down (forward.z = -sin pitch).
                if(walking) player->aim(double(event.motion.xrel), double(event.motion.yrel));   // legacy aim(): 0.003 rad per count, per-event clamp
                else {
                    pose.yaw += double(event.motion.xrel) * settings.sensitivity;
                    pose.pitch = std::clamp(pose.pitch + double(event.motion.yrel) * settings.sensitivity, -1.5, 1.5);
                }
            }
        }
        if(!running) break;
        simulation.advance(timer.lap(), [&](double dt) {
            if(input[cat::Action::pause].pressed) running = false;
            if(!fixed_time) sim_time += dt;
            if(walking && pausing && !captured) {   // N6.5: paused while the mouse is released (no game time passes)
                tapped_keys = 0;
                input.end_tick();
                return;
            }
            if(walking) {   // one deterministic 1/120 s player tick; mouse look was applied as events arrived
                played = true;
                int pw = 0, ph = 0;
                const double aspect = SDL_GetWindowSizeInPixels(window, &pw, &ph) && pw > 0 && ph > 0 ? double(pw) / double(ph) : 16.0 / 9.0;
                session->step({player_keys | tapped_keys, 0, 0}, aspect);
                tapped_keys = 0;
                selftest_slid = selftest_slid || player->stance() == cat::physics::Stance::slide;
                selftest_crouched = selftest_crouched || player->stance() == cat::physics::Stance::crouch;
                selftest_captured = selftest_captured || captured;
                input.end_tick();
                return;
            }
            // Debug fly camera for map inspection (no collision).
            const double speed = (input[cat::Action::sprint].held ? 24.0 : 8.0) * dt;
            const double ca = std::cos(pose.yaw), sa = std::sin(pose.yaw);
            const double f = (input[cat::Action::forward].held ? 1 : 0) - (input[cat::Action::backward].held ? 1 : 0);
            const double r = (input[cat::Action::right].held ? 1 : 0) - (input[cat::Action::left].held ? 1 : 0);
            const double u = (input[cat::Action::jump].held ? 1 : 0) - (input[cat::Action::crouch].held ? 1 : 0);
            pose.x += (ca * f - sa * r) * speed; pose.y += (sa * f + ca * r) * speed; pose.z += u * speed;
            input.end_tick();
        });
        if(!running) break;
        int pw = 0, ph = 0;
        if(scene && SDL_GetWindowSizeInPixels(window, &pw, &ph) && pw > 0 && ph > 0) {
            const std::pair<std::uint32_t, std::uint32_t> size{static_cast<std::uint32_t>(pw), static_cast<std::uint32_t>(ph)};
            cat::FrameView view{walking ? player_camera(*player, size) : camera_for(pose, size), walking ? player->now() : sim_time, features};
            if(walking && overlay) {
                if(pausing && !captured) {
                    session->pause_title = played ? "PAUSED" : "CAT ARENA: FERAL CLAW";
                    session->pause_help = std::string("CLICK TO PLAY|") + find_map(map)->title +
                                          "|WASD MOVE - MOUSE AIM - LEFT BUTTON FIRE|SHIFT SPRINT - SPACE JUMP - CTRL CROUCH - C SLIDE|R RELOAD - Q OR TAB SWAP WEAPON|"
                                          "1 2 3 4 CHANGE MAP - F5 RESTART - F11 FULLSCREEN|ESC QUITS";
                } else session->pause_title.clear();
                const auto built = session->build(size); session->light(view); renderer.draw(view, built);
            } else renderer.draw(view);
        }
        else renderer.draw(cat::Snapshot{simulation.ticks(), simulation.alpha()});
        if(frames > 0 && ++rendered >= frames && !(input_selftest && simulation.ticks() < 800)) running = false;
        if(resize_test && frames >= 4 && rendered == frames / 2) {   // forces swapchain + target recreation on the active path
            if(!SDL_SetWindowSize(window, settings.width * 3 / 4, settings.height * 3 / 4)) throw std::runtime_error(SDL_GetError());
            SDL_SyncWindow(window);
            cat::log("info", "app", "Resize test: requested " + std::to_string(settings.width * 3 / 4) + "x" + std::to_string(settings.height * 3 / 4));
        }
        if(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) SDL_Delay(20);
        else if(settings.frame_cap > 0) std::this_thread::sleep_until(start + std::chrono::nanoseconds(1'000'000'000 / settings.frame_cap));
    }
    if(report) {
        std::ofstream r(*report);
        const auto dev = renderer.report();
        r << "{\"device\": \"" << json_escape(dev.device) << "\", \"api\": \"" << dev.api << "\", \"colorFormat\": \"" << dev.color_format
          << "\", \"depthFormat\": \"" << dev.depth_format << "\", \"presentMode\": \"" << dev.present_mode << "\", \"validationLayer\": "
          << (dev.validation ? "true" : "false") << ", \"vulkan\": " << vulkan_json(dev, "") << ", \"finalExtent\": [" << renderer.extent().first << ", " << renderer.extent().second
          << "], \"map\": \"" << map << "\", \"mapSwitches\": " << map_switches << ", \"framesSubmitted\": " << rendered << ", \"ticks\": " << simulation.ticks();
        if(player) r << ", \"player\": {\"feet\": [" << player->state().x << ", " << player->state().y << ", " << player->state().z << "], \"ground\": " << (player->state().ground ? "true" : "false") << "}";
        if(session) r << ", \"combat\": " << session->json() << ", \"overlay\": " << (overlay ? "true" : "false");
        r
          << ", \"validationWarnings\": " << renderer.validation_warnings() << ", \"validationErrors\": " << renderer.validation_errors() << "}\n";
    }
    int selftest_result = 0;
    if(input_selftest && player) {
        const double moved = std::hypot(player->state().x - selftest_x0, player->state().y - selftest_y0), turned = player->state().a - selftest_yaw0;
        const bool ok = selftest_captured && selftest_motions == 40 && std::abs(turned - 40 * 5 * cat::physics::kLookYaw) < 1e-9 && moved > 3 && player->jumps() >= 1 && selftest_slid && selftest_crouched && session->volleys >= 1 && session->reloads >= 1 && session->swaps == 1;
        cat::log(ok ? "info" : "error", "app", std::string("Input self-test ") + (ok ? "PASS" : "FAIL") + ": relativeMouse=" + (selftest_captured ? "on" : "off") + " mouseEvents=" +
                 std::to_string(selftest_motions) + " yawChange=" + std::to_string(turned) + " (expected 0.6) moved=" + std::to_string(moved) + "m jumps=" + std::to_string(player->jumps()) +
                 " slid=" + (selftest_slid ? "yes" : "no") + " crouched=" + (selftest_crouched ? "yes" : "no") + " volleys=" + std::to_string(session->volleys) +
                 " reloads=" + std::to_string(session->reloads) + " swaps=" + std::to_string(session->swaps) + " ticks=" + std::to_string(simulation.ticks()));
        selftest_result = ok ? 0 : 3;
    }
 if(controls_selftest) {
        const auto& c = controls;
        const bool ok = c.paused_ok && c.resumed_ok && c.restarted_ok && c.windowed_again && c.quit_by_esc && c.stage == 10;
        cat::log(ok ? "info" : "error", "app", std::string("Controls self-test ") + (ok ? "PASS" : "FAIL") + ": pausedByEsc=" + (c.paused_ok ? "yes" : "no") + " resumedByClick=" + (c.resumed_ok ? "yes" : "no") +
                                                " f11Fullscreen=" + (c.fullscreen_seen ? "yes" : "no (not granted by this display server)") + " f11Windowed=" + (c.windowed_again ? "yes" : "no") +
                                                " f5Restart=" + (c.restarted_ok ? "yes" : "no") + " quitByEsc=" + (c.quit_by_esc && c.stage == 10 ? "yes" : "no"));
        if(!ok) selftest_result = 3;
    }
    cat::log("info", "app", "Clean shutdown; ticks=" + std::to_string(simulation.ticks()) + " frames=" + std::to_string(rendered) +
                                " validationWarnings=" + std::to_string(renderer.validation_warnings()) + " validationErrors=" + std::to_string(renderer.validation_errors()));
    return renderer.validation_errors() ? 2 : selftest_result;
 } catch(const std::exception& e) {
    cat::log("error", "app", e.what());
    if(g_error_dialog) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cat Arena: Feral Claw", e.what(), nullptr);
    return 1;
 }
}
