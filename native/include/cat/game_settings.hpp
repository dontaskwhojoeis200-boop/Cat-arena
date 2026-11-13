#pragma once
// N7: the legacy settings panel as data. Every control of the six legacy tabs (index.html 627-806,
// moved / extended by mobile-hud.js and graphics-menu.js) with its legacy id, label, range or
// options and default value, as recorded from the running legacy page
// (tests/fixtures/n7/legacy_settings.json), plus what the native build does with it.
// Values are stored by legacy id, persisted as "id=value" lines. GPU/SDL independent.
#include <cstdint>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cat {

enum class SettingKind : std::uint8_t { range, select, toggle, action };
enum class SettingsTab : std::uint8_t { view, graphics, performance, input, audio, hud };
// Native status of a control: applied as in the legacy game, applied with a stated difference, or
// stored only (shown disabled with the reason) because the native renderer does not have the feature yet.
enum class Support : std::uint8_t { applied, partial, stored };

struct SettingOption { std::string value, label; };
struct SettingDef {
    std::string id;                  // legacy element id
    SettingsTab tab;
    SettingKind kind;
    std::string label;
    std::string section;             // heading shown above this control ("" = none)
    std::string fallback;            // default value (legacy default)
    double min{}, max{}, step{1};
    std::vector<SettingOption> options;
    Support support{Support::applied};
    std::string note;                // help text / why it is not applied natively
};

const std::vector<SettingDef>& setting_defs();
const SettingDef* find_setting(std::string_view id);
const char* tab_title(SettingsTab t);
std::string format_value(const SettingDef& d, const std::string& value);   // the legacy value read-out ("105°", "LEVEL · 0°", "UNLIMITED", ...)

struct HudPlacement { double x{}, y{}; int size{100}, opacity{100}; bool operator==(const HudPlacement&) const = default; };   // legacy hudRecords: x, y as fractions of the viewport

class GameSettings {
public:
    GameSettings();   // legacy defaults
    std::string text(std::string_view id) const;
    double number(std::string_view id) const;
    bool flag(std::string_view id) const;
    // Validated set: ranges are clamped and snapped to the step, options must exist; returns false when rejected.
    bool set(std::string_view id, std::string_view value);
    bool set_number(std::string_view id, double v);
    bool set_flag(std::string_view id, bool v) { return set(id, v ? "true" : "false"); }
    void reset(std::string_view id);
    // graphics-menu.js: a preset writes its renderer values; changing one of them switches the preset to "custom".
    void apply_preset(std::string_view preset);
    void restore_normal_graphics();   // gfxReset: brightness 1, preset normal
    void restore_viewmodel();         // vmReset
    std::map<std::string, HudPlacement>& hud_layout() { return hud_; }
    const std::map<std::string, HudPlacement>& hud_layout() const { return hud_; }
    std::string map{"helios-crucible"};   // voidArena.map
    std::string player_name{"Marine Cat"};
    void save(std::ostream& out) const;
    static GameSettings load(std::istream& in);   // unknown or invalid lines are ignored
    bool operator==(const GameSettings&) const = default;
private:
    bool assign(std::string_view id, std::string_view value, bool react);   // react: preset logic of graphics-menu.js
    std::map<std::string, std::string, std::less<>> values_;
    std::map<std::string, HudPlacement> hud_;
};

// The legacy HUD_ITEMS (mobile-hud.js 16466): editable HUD elements, in the editor's order.
struct HudItem { const char* id; const char* name; bool touch_only; };
const std::vector<HudItem>& hud_items();

}  // namespace cat
