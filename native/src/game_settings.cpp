// N7: the legacy settings as data (see include/cat/game_settings.hpp).
#include "cat/game_settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <istream>
#include <ostream>
#include <sstream>

namespace cat {
namespace {
using K = SettingKind;
using T = SettingsTab;
using S = Support;
SettingDef range(T tab, std::string id, std::string label, double def, double min, double max, double step, S sup = S::applied, std::string note = {}, std::string section = {}) {
    std::ostringstream v; v << def;
    SettingDef d{std::move(id), tab, K::range, std::move(label), std::move(section), v.str(), min, max, step, {}, sup, std::move(note)};
    return d;
}
SettingDef select(T tab, std::string id, std::string label, std::string def, std::vector<SettingOption> options, S sup = S::applied, std::string note = {}, std::string section = {}) {
    return {std::move(id), tab, K::select, std::move(label), std::move(section), std::move(def), 0, 0, 1, std::move(options), sup, std::move(note)};
}
SettingDef toggle(T tab, std::string id, std::string label, bool def, S sup = S::applied, std::string note = {}, std::string section = {}) {
    return {std::move(id), tab, K::toggle, std::move(label), std::move(section), def ? "true" : "false", 0, 1, 1, {}, sup, std::move(note)};
}
SettingDef action(T tab, std::string id, std::string label, S sup = S::applied, std::string note = {}, std::string section = {}) {
    return {std::move(id), tab, K::action, std::move(label), std::move(section), "", 0, 0, 1, {}, sup, std::move(note)};
}
const std::string kN10 = "Stored; the native renderer does not have this yet (N10).";

std::vector<SettingDef> make_defs() {
    std::vector<SettingDef> d;
    // ---- VIEW & CAMERA
    d.push_back(range(T::view, "fovControl", "HORIZONTAL FOV", 105, 75, 125, 1, S::applied, "Adjust between 75° and 125°. World FOV. The weapon has a separately tuned FOV in Retro Reference Viewmodel below."));
    d.push_back(select(T::view, "crosshairStyle", "CROSSHAIR STYLE", "classic", {{"classic", "Classic (4 ticks)"}, {"dot", "Dot"}, {"plus", "Precision plus"}, {"ring", "Circle + dot"}, {"bullseye", "Bullseye"}, {"circle", "Circle only"}, {"t", "T-shaped"}, {"none", "Off"}}));
    d.push_back(select(T::view, "crosshairColor", "CROSSHAIR COLOR", "#f2cf93", {{"#f2cf93", "Classic amber"}, {"#6dff77", "Neon green"}, {"#f7f7e9", "White"}, {"#65e9ff", "Cyan"}, {"#ff6480", "Red"}}));
    d.push_back(range(T::view, "vmAngleOffset", "WEAPON DIRECTION (YAW)", 0, -20, 20, 1, S::applied, "The weapon uses an independently calibrated 3-D camera. The weapon stays LEVEL (zero sideways roll). Direction, muzzle position and size use independent weapon-camera settings; shots still follow the world crosshair.", "RETRO REFERENCE VIEWMODEL"));
    d.push_back(range(T::view, "vmSizeFov", "WEAPON SIZE (INDEPENDENT FOV)", 60, 48, 80, 1));
    d.push_back(range(T::view, "vmVerticalOffset", "WEAPON VERTICAL POSITION", 0, -15, 15, 1));
    d.push_back(action(T::view, "vmReset", "RESTORE LEVEL VIEWMODEL DEFAULTS"));
    d.push_back(range(T::view, "motionStrength", "VIEW & WEAPON MOTION", 100, 0, 200, 5, S::applied, "Responsive view and weapon motion: footstep bob, run pitch and strafe roll, landing dip, step smoothing and weapon sway. 100% uses the standard motion settings; 0% turns bobbing off."));
    // ---- GRAPHICS (the benchmark toggles were moved here by mobile-hud.js)
    d.push_back(toggle(T::graphics, "perfFps", "Show FPS", true, S::applied, {}, "BENCHMARK · FPS / CPU / GPU / RAM / VRAM"));
    d.push_back(toggle(T::graphics, "perfCpu", "Show CPU frame time", false));
    d.push_back(toggle(T::graphics, "perfGpu", "Show GPU frame time", false, S::applied, "Vulkan timestamp queries; shown as unavailable where the GPU queue has none."));
    d.push_back(toggle(T::graphics, "perfRam", "Live JS heap (RAM)", true, S::partial, "Native: process memory in use (resident set / working set); there is no JavaScript heap."));
    d.push_back(toggle(T::graphics, "perfAppMem", "Active webpage RAM (if available)", true, S::partial, "Native: peak process memory."));
    d.push_back(toggle(T::graphics, "perfVram", "Tracked GPU allocations (estimate)", true, S::applied, "Native: exact sum of the renderer's Vulkan memory allocations (drivers may reserve more)."));
    d.push_back(select(T::graphics, "gfxPreset", "GRAPHICS", "normal", {{"high", "HIGH QUALITY"}, {"normal", "NORMAL"}, {"fast", "FAST"}, {"fastest", "FASTEST"}, {"custom", "CUSTOM"}}, S::partial,
                       "Fine-tune the look and performance of your arena. Changes apply immediately; picking any option switches the preset to CUSTOM. Native: the preset writes every value below; only the applied ones change the picture.", "CAT ARENA · GRAPHICS"));
    d.push_back(select(T::graphics, "lightQuality", "LIGHTING", "1", {{"0", "VERTEX · r_vertexLight, per-vertex bake"}, {"1", "LIGHTMAP · baked per-surface shadows"}, {"2", "LIGHT VOLUME · classic 3-D grid"}}, S::partial,
                       "Native: LIGHTMAP is the renderer's lighting; VERTEX and LIGHT VOLUME world lighting are N10.", "LIGHTING & DISPLAY"));
    d.push_back(toggle(T::graphics, "gfxOverbright", "Overbright lighting", true, S::stored, kN10));
    d.push_back(toggle(T::graphics, "gfxEntityGrid", "Light grid on models [entity lighting]", true));
    d.push_back(toggle(T::graphics, "gfxDynamicLights", "Dynamic lights", true));
    d.push_back(toggle(T::graphics, "gfxFlares", "Light flares / coronas", true, S::stored, kN10));
    d.push_back(toggle(T::graphics, "gfxSky", "High quality sky & clouds", true, S::partial, "Native: off hides the cloud layers (r_fastsky)."));
    d.push_back(range(T::graphics, "gfxGamma", "BRIGHTNESS", 1, .6, 1.8, .05, S::stored, kN10));
    d.push_back(range(T::graphics, "gfxPicmip", "TEXTURE DETAIL", 0, 0, 3, 1, S::stored, kN10, "TEXTURES"));
    d.push_back(select(T::graphics, "gfxBits", "TEXTURE QUALITY", "32", {{"32", "32-BIT · smooth"}, {"16", "16-BIT · retro banding"}}, S::stored, kN10));
    d.push_back(select(T::graphics, "gfxFilter", "TEXTURE FILTER", "trilinear", {{"trilinear", "TRILINEAR · LINEAR_MIPMAP_LINEAR"}, {"bilinear", "BILINEAR · LINEAR_MIPMAP_NEAREST"}, {"nearest", "NEAREST · crisp pixels"}}, S::stored, kN10));
    d.push_back(select(T::graphics, "gfxAniso", "ANISOTROPY", "4", {{"0", "OFF"}, {"2", "2×"}, {"4", "4×"}, {"8", "8×"}, {"16", "16×"}}, S::stored, "Native: fixed at 4× (the normal preset) where supported; switching is N10."));
    d.push_back(select(T::graphics, "gfxShadows", "SHADOWS", "1", {{"0", "OFF"}, {"1", "BLOB · soft disc"}, {"3", "PROJECTED · body silhouette"}}, S::applied, {}, "EFFECTS (GAME OPTIONS)"));
    d.push_back(select(T::graphics, "gfxEffects", "EFFECTS DETAIL", "1", {{"1", "HIGH · full particles"}, {"0.7", "MEDIUM"}, {"0.45", "LOW · fewer particles"}}, S::stored, kN10));
    d.push_back(toggle(T::graphics, "gfxMarks", "Marks on walls", true, S::stored, kN10));
    d.push_back(toggle(T::graphics, "gfxBrass", "Ejecting brass", true, S::stored, kN10));
    d.push_back(toggle(T::graphics, "gfxBlood", "Blood [com_blood]", true, S::stored, kN10));
    d.push_back(toggle(T::graphics, "gfxGibs", "Gibs", true, S::stored, kN10));
    d.push_back(range(T::graphics, "gfxRail", "RAIL TRAIL TIME", .6, .2, 1.6, .05, S::stored, kN10));
    d.push_back(select(T::graphics, "gfxLmFilter", "LIGHTMAP FILTER", "bilinear", {{"bilinear", "BILINEAR · SMOOTH"}, {"nearest", "NEAREST · visible luxels"}}, S::stored, kN10, "ID TECH 3 RENDERER"));
    d.push_back(range(T::graphics, "gfxLmIntensity", "LIGHTMAP INTENSITY", 1, .6, 1.6, .05, S::stored, kN10));
    d.push_back(select(T::graphics, "gfxCurves", "CURVE DETAIL", "2", {{"0", "LOW · 16 cm error"}, {"1", "MEDIUM · 6 cm"}, {"2", "HIGH · 2.5 cm"}, {"3", "ULTRA · 1 cm"}}, S::stored, kN10));
    d.push_back(toggle(T::graphics, "gfxDetail", "Detail textures", true, S::stored, kN10));
    d.push_back(toggle(T::graphics, "gfxMirrors", "Mirrors & portal views", true, S::stored, "Mirrors and portal views are not drawn natively yet (N10)."));
    d.push_back(select(T::graphics, "gfxPortalScale", "MIRROR / PORTAL RESOLUTION", "0.5", {{"0.25", "25% · fastest"}, {"0.5", "50%"}, {"1", "100% · sharpest"}}, S::stored, kN10));
    d.push_back(toggle(T::graphics, "gfxVis", "PVS visibility culling", true, S::partial, "Native: switches the renderer's cluster culling."));
    d.push_back(action(T::graphics, "gfxDebugOpen", "OPEN GRAPHICS DEBUG VIEW · F9", S::partial, "Native: F9 cycles the renderer's debug views (lightmap only, fullbright, normals, off)."));
    d.push_back(action(T::graphics, "gfxReset", "RESTORE NORMAL GRAPHICS"));
    // ---- PERFORMANCE
    d.push_back(select(T::performance, "worldCullingMode", "WORLD VISIBILITY", "balanced", {{"balanced", "BALANCED · conservative sector culling"}, {"quality", "QUALITY · smooth, frustum only"}, {"legacy", "FAST · legacy aggressive culling"}}, S::stored, "Native: one cluster-culling mode (PVS toggle in Graphics); the three legacy modes are N10."));
    d.push_back(select(T::performance, "fogQuality", "FOG QUALITY", "1", {{"0", "OFF · maximum visibility"}, {"0.5", "LOW · lighter fog"}, {"1", "STANDARD · atmospheric"}, {"1.25", "DENSE · cinematic"}}, S::partial, "Native: OFF removes the fog; the other levels draw the standard fog (densities are N10)."));
    d.push_back(range(T::performance, "sceneResolution", "SCENE RESOLUTION", 720, 480, 1080, 10, S::stored, "Native: renders at the window resolution; render scaling is N10."));
    d.push_back(select(T::performance, "msaaSamples", "MULTISAMPLE AA", "0", {{"0", "OFF · fastest"}, {"2", "2× MSAA"}, {"4", "4× MSAA"}, {"8", "8× MSAA"}}, S::stored, kN10));
    d.push_back(toggle(T::performance, "fxaaEnabled", "FXAA (fast post-process anti-aliasing)", false, S::stored, kN10));
    d.push_back(toggle(T::performance, "adaptiveResolution", "Adaptive resolution (optional)", false, S::stored, kN10));
    d.push_back(select(T::performance, "targetFps", "ADAPTIVE FPS TARGET", "120", {{"90", "90 FPS"}, {"120", "120 FPS"}, {"144", "144 FPS"}, {"165", "165 FPS"}, {"200", "200 FPS"}, {"240", "240 FPS"}}, S::stored, kN10));
    d.push_back(toggle(T::performance, "vsyncEnabled", "VSync preference", true, S::applied, "Native: FIFO when on; mailbox or immediate presentation when off (where the display offers them)."));
    d.push_back(range(T::performance, "fpsLimit", "FRAME RATE LIMIT", 241, 0, 241, 1, S::partial, "241 = unlimited. Native: limits below 10 frames per second are raised to 10 so the menus stay usable."));
    // ---- MOBILE / INPUT
    d.push_back(select(T::input, "inputMode", "CONTROL MODE", "auto", {{"auto", "AUTO · last input used"}, {"touch", "TOUCH"}, {"kbm", "KEYBOARD & MOUSE / DeX"}}));
    d.push_back(range(T::input, "touchSensitivity", "LOOK SENSITIVITY", 100, 25, 250, 5));
    d.push_back(range(T::input, "touchSize", "TOUCH BUTTON SIZE", 100, 75, 125, 5));
    d.push_back(range(T::input, "touchOpacity", "BUTTON OPACITY", 80, 25, 100, 5));
    d.push_back(toggle(T::input, "touchInvert", "Invert touch look Y", false));
    d.push_back(select(T::input, "sprintMode", "SPRINT BEHAVIOR", "hold", {{"hold", "HOLD · Shift / Sprint button"}, {"toggle", "TOGGLE · Shift / Sprint button"}, {"auto", "AUTO · while moving"}}));
    // ---- AUDIO
    d.push_back(range(T::audio, "masterVolume", "MASTER VOLUME", 70, 0, 100, 1));
    d.push_back(range(T::audio, "weaponVolume", "WEAPONS & EFFECTS", 85, 0, 100, 1));
    d.push_back(range(T::audio, "enemyVolume", "ENEMY VOICES & ATTACKS", 80, 0, 100, 1));
    d.push_back(range(T::audio, "musicVolume", "SOUNDTRACK", 46, 0, 100, 1));
    d.push_back(toggle(T::audio, "musicEnabled", "Play ASHEN DRIVE · original instrumental theme", true));
    d.push_back(action(T::audio, "testAudio", "TEST AUDIO"));
    // ---- HUD / UI
    d.push_back(range(T::hud, "hudScale", "HUD SIZE", 115, 85, 145, 5));
    d.push_back(toggle(T::hud, "showScore", "Show kill/death counter", true));
    d.push_back(toggle(T::hud, "showHitFeedback", "Show hitmarkers and damage numbers", true));
    d.push_back(toggle(T::hud, "showControlTips", "Show keyboard control tips", false));
    d.push_back(toggle(T::hud, "showStance", "Show movement stance", true));
    d.push_back(action(T::hud, "editHudLayout", "EDIT HUD POSITION / SIZE / OPACITY", S::applied, {}, "CUSTOM HUD LAYOUT"));
    d.push_back(action(T::hud, "resetHudLayout", "RESET ALL HUD POSITIONS"));
    d.push_back(toggle(T::hud, "autoFullscreen", "Fullscreen when launching", true));
    d.push_back(action(T::hud, "fullscreenNow", "ENTER FULLSCREEN NOW"));
    return d;
}
const std::map<std::string, std::map<std::string, std::string>>& presets() {   // Q3R_PRESETS (2787), mapped to the controls
    static const std::map<std::string, std::map<std::string, std::string>> p{
        {"high", {{"gfxPicmip", "0"}, {"gfxFilter", "trilinear"}, {"gfxAniso", "8"}, {"gfxBits", "32"}, {"gfxDynamicLights", "true"}, {"gfxFlares", "true"}, {"gfxSky", "true"}, {"gfxMarks", "true"}, {"gfxShadows", "3"}, {"gfxBrass", "true"}, {"gfxGibs", "true"}, {"gfxBlood", "true"}, {"gfxEffects", "1"}, {"gfxEntityGrid", "true"}, {"gfxOverbright", "true"}, {"gfxCurves", "3"}, {"gfxDetail", "true"}, {"gfxPortalScale", "1"}, {"gfxMirrors", "true"}, {"lightQuality", "1"}}},
        {"normal", {{"gfxPicmip", "0"}, {"gfxFilter", "trilinear"}, {"gfxAniso", "4"}, {"gfxBits", "32"}, {"gfxDynamicLights", "true"}, {"gfxFlares", "true"}, {"gfxSky", "true"}, {"gfxMarks", "true"}, {"gfxShadows", "1"}, {"gfxBrass", "true"}, {"gfxGibs", "true"}, {"gfxBlood", "true"}, {"gfxEffects", "1"}, {"gfxEntityGrid", "true"}, {"gfxOverbright", "true"}, {"gfxCurves", "2"}, {"gfxDetail", "true"}, {"gfxPortalScale", "0.5"}, {"gfxMirrors", "true"}, {"lightQuality", "1"}}},
        {"fast", {{"gfxPicmip", "1"}, {"gfxFilter", "bilinear"}, {"gfxAniso", "0"}, {"gfxBits", "32"}, {"gfxDynamicLights", "true"}, {"gfxFlares", "false"}, {"gfxSky", "true"}, {"gfxMarks", "true"}, {"gfxShadows", "1"}, {"gfxBrass", "false"}, {"gfxGibs", "true"}, {"gfxBlood", "true"}, {"gfxEffects", "0.7"}, {"gfxEntityGrid", "true"}, {"gfxOverbright", "false"}, {"gfxCurves", "1"}, {"gfxDetail", "false"}, {"gfxPortalScale", "0.5"}, {"gfxMirrors", "true"}, {"lightQuality", "1"}}},
        {"fastest", {{"gfxPicmip", "2"}, {"gfxFilter", "nearest"}, {"gfxAniso", "0"}, {"gfxBits", "16"}, {"gfxDynamicLights", "false"}, {"gfxFlares", "false"}, {"gfxSky", "false"}, {"gfxMarks", "false"}, {"gfxShadows", "0"}, {"gfxBrass", "false"}, {"gfxGibs", "false"}, {"gfxBlood", "true"}, {"gfxEffects", "0.45"}, {"gfxEntityGrid", "false"}, {"gfxOverbright", "false"}, {"gfxCurves", "0"}, {"gfxDetail", "false"}, {"gfxPortalScale", "0.25"}, {"gfxMirrors", "false"}, {"lightQuality", "0"}}},
    };
    return p;
}
std::string number_text(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.10g", v);
    return b;
}
std::string signed_int(double v) { const int n = static_cast<int>(std::lround(v)); return (n > 0 ? "+" : "") + std::to_string(n); }
}  // namespace

const std::vector<SettingDef>& setting_defs() { static const std::vector<SettingDef> d = make_defs(); return d; }
const SettingDef* find_setting(std::string_view id) {
    for(const auto& d : setting_defs()) if(d.id == id) return &d;
    return nullptr;
}
const char* tab_title(SettingsTab t) {
    switch(t) {
        case SettingsTab::view: return "VIEW & CAMERA";
        case SettingsTab::graphics: return "GRAPHICS";
        case SettingsTab::performance: return "PERFORMANCE";
        case SettingsTab::input: return "MOBILE / INPUT";
        case SettingsTab::audio: return "AUDIO";
        case SettingsTab::hud: return "HUD / UI";
    }
    return "";
}
const std::vector<HudItem>& hud_items() {
    static const std::vector<HudItem> items{{"leftStats", "HEALTH / SPEED", false}, {"weaponBox", "AMMO / WEAPON", false}, {"waveDisplay", "WAVE STATUS", false}, {"score", "ELIMINATIONS", false},
        {"brand", "GAME TITLE", false}, {"stanceLabel", "STANCE", false}, {"perfOverlay", "PERFORMANCE", false}, {"mapbutton", "MAP", false}, {"fovToggle", "SETTINGS", false},
        {"stick", "MOVE JOYSTICK", true}, {"fire", "FIRE", true}, {"jump", "JUMP", true}, {"swap", "SWAP", true}, {"touchSprint", "SPRINT", true}, {"touchCrouch", "SLIDE", true},
        {"touchReload", "RELOAD", true}, {"touchRevive", "REVIVE", true}};
    return items;
}

std::string format_value(const SettingDef& d, const std::string& value) {
    if(d.kind == SettingKind::select) {
        for(const auto& o : d.options) if(o.value == value) return o.label;
        return value;
    }
    if(d.kind == SettingKind::toggle) return value == "true" ? "ON" : "OFF";
    double v = 0;
    try { v = std::stod(value); } catch(...) { return value; }
    char b[48];
    if(d.id == "fovControl" || d.id == "vmSizeFov") return std::to_string(static_cast<int>(std::lround(v))) + "°";
    if(d.id == "vmAngleOffset") return std::lround(v) == 0 ? "LEVEL · 0°" : signed_int(v) + "°";
    if(d.id == "vmVerticalOffset") return std::lround(v) == 0 ? "LEVEL · 0%" : signed_int(v) + "%";
    if(d.id == "gfxGamma" || d.id == "gfxLmIntensity") { std::snprintf(b, sizeof b, "%.2f", v); return b; }
    if(d.id == "gfxRail") { std::snprintf(b, sizeof b, "%.2f s", v); return b; }
    if(d.id == "gfxPicmip") { static const char* n[] = {"HIGH", "MEDIUM", "LOW", "LOWEST"}; return n[std::clamp(static_cast<int>(std::lround(v)), 0, 3)]; }
    if(d.id == "sceneResolution") return std::to_string(static_cast<int>(std::lround(v))) + "p";
    if(d.id == "fpsLimit") { const int n = static_cast<int>(std::lround(v)); return n >= 241 ? "UNLIMITED" : n == 0 ? "0 (PAUSE)" : std::to_string(n); }
    return std::to_string(static_cast<int>(std::lround(v))) + "%";
}

GameSettings::GameSettings() {
    for(const auto& d : setting_defs()) if(d.kind != SettingKind::action) values_[d.id] = d.fallback;
}
std::string GameSettings::text(std::string_view id) const {
    const auto it = values_.find(id);
    return it == values_.end() ? std::string() : it->second;
}
double GameSettings::number(std::string_view id) const {
    try { return std::stod(text(id)); } catch(...) { return 0; }
}
bool GameSettings::flag(std::string_view id) const { return text(id) == "true"; }
bool GameSettings::set(std::string_view id, std::string_view value) { return assign(id, value, true); }
bool GameSettings::assign(std::string_view id, std::string_view value, bool react) {
    const SettingDef* d = find_setting(id);
    if(!d || d->kind == SettingKind::action) return false;
    std::string v(value);
    if(d->kind == SettingKind::toggle) { if(v != "true" && v != "false") return false; }
    else if(d->kind == SettingKind::select) { if(std::none_of(d->options.begin(), d->options.end(), [&](const SettingOption& o) { return o.value == v; })) return false; }
    else {
        double x = 0;
        try { std::size_t used = 0; x = std::stod(v, &used); if(used != v.size() || !std::isfinite(x)) return false; } catch(...) { return false; }
        x = std::clamp(x, d->min, d->max);
        x = d->min + std::round((x - d->min) / d->step) * d->step;   // snap like <input type=range>
        x = std::clamp(std::round(x * 1e6) / 1e6, d->min, d->max);
        v = number_text(x);
    }
    values_[d->id] = v;
    if(!react) return true;
    // graphics-menu.js: picking a preset-controlled option switches the preset to CUSTOM
    if(d->id != "gfxPreset") {
        const std::string preset = text("gfxPreset");
        const auto p = presets().find(preset);
        if(p != presets().end()) {
            const auto it = p->second.find(d->id);
            if(it != p->second.end() && it->second != v) values_["gfxPreset"] = "custom";
        }
    } else if(v != "custom") apply_preset(v);
    return true;
}
bool GameSettings::set_number(std::string_view id, double v) { return set(id, number_text(v)); }
void GameSettings::reset(std::string_view id) { if(const auto* d = find_setting(id); d && d->kind != SettingKind::action) values_[d->id] = d->fallback; }
void GameSettings::apply_preset(std::string_view preset) {
    const auto p = presets().find(std::string(preset));
    if(p == presets().end()) { values_["gfxPreset"] = "custom"; return; }
    for(const auto& [id, value] : p->second) values_[id] = value;   // brightness is kept (applyPreset restores S.gamma)
    values_["gfxPreset"] = std::string(preset);
}
void GameSettings::restore_normal_graphics() { values_["gfxGamma"] = "1"; apply_preset("normal"); }
void GameSettings::restore_viewmodel() { values_["vmAngleOffset"] = "0"; values_["vmSizeFov"] = "60"; values_["vmVerticalOffset"] = "0"; }

void GameSettings::save(std::ostream& out) const {
    out << "# Cat Arena: Feral Claw settings (legacy control ids)\n";
    for(const auto& [k, v] : values_) out << k << '=' << v << '\n';
    out << "map=" << map << "\nplayerName=" << player_name << '\n';
    for(const auto& [id, r] : hud_) out << "hud." << id << '=' << number_text(r.x) << ',' << number_text(r.y) << ',' << r.size << ',' << r.opacity << '\n';
}
GameSettings GameSettings::load(std::istream& in) {
    GameSettings s;
    std::string line;
    while(std::getline(in, line)) {
        if(line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if(eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if(key == "map") { if(!value.empty() && value.size() < 64) s.map = value; continue; }
        if(key == "playerName") { if(value.size() < 32) s.player_name = value; continue; }
        if(key.rfind("hud.", 0) == 0) {
            const std::string id = key.substr(4);
            if(std::none_of(hud_items().begin(), hud_items().end(), [&](const HudItem& h) { return id == h.id; })) continue;
            HudPlacement r; char c1 = 0, c2 = 0, c3 = 0;
            std::istringstream v(value);
            if(v >> r.x >> c1 >> r.y >> c2 >> r.size >> c3 >> r.opacity && c1 == ',' && c2 == ',' && c3 == ',' && std::isfinite(r.x) && std::isfinite(r.y)) {
                r.x = std::clamp(r.x, 0.0, 1.0); r.y = std::clamp(r.y, 0.0, 1.0); r.size = std::clamp(r.size, 45, 200); r.opacity = std::clamp(r.opacity, 10, 100);   // hudLoad clamps
                s.hud_[id] = r;
            }
            continue;
        }
        s.assign(key, value, false);   // stored values as saved (no preset side effects)
    }
    in.clear();
    return s;
}

}  // namespace cat
