// Cat Arena: Feral Claw — menu module (ui) for the Quake3e engine. N10-Q3E stage B.
//
// A native module written in C++20 behind the engine's two C entry points (dllEntry, vmMain).
// Stage B scope: a branded main menu (play the test arena, quit), an in-game menu (resume, leave,
// quit) and the loading screen. Text uses the Cat Arena charset texture gfx/2d/bigchars (generated
// from the shipped Liberation Mono font by tools/q3e/make_base.py). The full N10 menus come later.
//
// Licensed under the GNU GPL v2 or later (docs/q3e/LICENSING.md).

extern "C" {
#include "../../ioq3/code/qcommon/q_shared.h"
#include "../../ioq3/code/renderercommon/tr_types.h"
#include "../../ioq3/code/ui/ui_public.h"
}
#include "ca_bridge.h"

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>

namespace {

// ---- engine calls -----------------------------------------------------------------------------
using Syscall = intptr_t(QDECL*)(intptr_t, ...);
Syscall g_syscall = nullptr;

int pass_float(float f) {
    int i;
    std::memcpy(&i, &f, sizeof i);
    return i;
}
void print(const char* text) { g_syscall(UI_PRINT, text); }
void exec(const char* text) { g_syscall(UI_CMD_EXECUTETEXT, EXEC_APPEND, text); }
int register_shader(const char* name) { return int(g_syscall(UI_R_REGISTERSHADERNOMIP, name)); }
void set_color(const float* rgba) { g_syscall(UI_R_SETCOLOR, rgba); }
void stretch_pic(float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader) {
    g_syscall(UI_R_DRAWSTRETCHPIC, pass_float(x), pass_float(y), pass_float(w), pass_float(h), pass_float(s1),
              pass_float(t1), pass_float(s2), pass_float(t2), shader);
}
void set_catcher(int c) { g_syscall(UI_KEY_SETCATCHER, c); }
int get_catcher() { return int(g_syscall(UI_KEY_GETCATCHER)); }
void get_glconfig(glconfig_t* g) { g_syscall(UI_GETGLCONFIG, g); }
void get_clientstate(uiClientState_t* s) { g_syscall(UI_GETCLIENTSTATE, s); }

// ---- key codes (Quake3e client/keycodes.h) ------------------------------------------------------
constexpr int kEnter = 13, kEscape = 27, kUp = 132, kDown = 133, kKpEnter = 169, kMouse1 = 178,
              kWheelDown = 183, kWheelUp = 184, kCharFlag = 1024;

// ---- state --------------------------------------------------------------------------------------
enum class Menu { none, main, ingame };
struct Item {
    const char* label;
    const char* command;   // console command; nullptr = close the menu
};
constexpr std::array kMainItems{Item{"PLAY  -  REACTOR BASTION", "map ca_reactor\n"}, Item{"PLAY  -  TEST ARENA", "map ca_test\n"},
                                Item{"QUIT", "quit\n"}};
constexpr std::array kIngameItems{Item{"RESUME", nullptr}, Item{"LEAVE ARENA", "disconnect\n"}, Item{"QUIT", "quit\n"}};

struct State {
    Menu menu = Menu::none;
    int selected = 0;
    float cursor_x = 320, cursor_y = 240;
    float scale_x = 1, scale_y = 1, bias_x = 0;   // 640x480 virtual screen, centred 4:3
    int vid_w = 640, vid_h = 480;
    int white = 0, chars = 0;
} g;

std::span<const Item> items() {
    if(g.menu == Menu::main) return kMainItems;
    if(g.menu == Menu::ingame) return kIngameItems;
    return {};
}

// ---- drawing (virtual 640x480 coordinates) -----------------------------------------------------
constexpr float kBrass[4] = {0.84f, 0.69f, 0.44f, 1.0f};
constexpr float kBrassDim[4] = {0.54f, 0.41f, 0.23f, 1.0f};
constexpr float kText[4] = {0.92f, 0.90f, 0.86f, 1.0f};
constexpr float kPlate[4] = {0.09f, 0.10f, 0.12f, 1.0f};
constexpr float kShade[4] = {0.0f, 0.0f, 0.0f, 0.62f};
constexpr float kSlash[4] = {0.77f, 0.23f, 0.17f, 1.0f};

void fill(float x, float y, float w, float h, const float* color) {
    set_color(color);
    stretch_pic(x * g.scale_x + g.bias_x, y * g.scale_y, w * g.scale_x, h * g.scale_y, 0, 0, 0, 0, g.white);
    set_color(nullptr);
}
void fill_screen(const float* color) {
    set_color(color);
    stretch_pic(0, 0, float(g.vid_w), float(g.vid_h), 0, 0, 0, 0, g.white);
    set_color(nullptr);
}
constexpr float kAdvance = 0.64f;   // glyph advance as a fraction of the cell (Liberation Mono)
float text_width(std::string_view s, float size) { return float(s.size()) * size * kAdvance; }
void text(float x, float y, std::string_view s, float size, const float* color) {
    set_color(color);
    for(unsigned char ch : s) {
        if(ch != ' ') {
            const float col = float(ch & 15) * 0.0625f, row = float(ch >> 4) * 0.0625f;
            stretch_pic(x * g.scale_x + g.bias_x, y * g.scale_y, size * g.scale_x, size * g.scale_y, col, row,
                        col + 0.0625f, row + 0.0625f, g.chars);
        }
        x += size * kAdvance;
    }
    set_color(nullptr);
}
void text_centered(float y, std::string_view s, float size, const float* color) {
    text(320 - text_width(s, size) / 2, y, s, size, color);
}

constexpr float kItemTop = 250, kItemStep = 36, kItemSize = 16;
int item_at(float x, float y) {
    const auto list = items();
    for(int i = 0; i < int(list.size()); ++i) {
        const float top = kItemTop + float(i) * kItemStep - 8;
        if(y >= top && y < top + kItemStep - 4 && x >= 160 && x <= 480) return i;
    }
    return -1;
}

void draw_menu() {
    const bool main = g.menu == Menu::main;
    if(main) fill_screen(kPlate);
    else fill_screen(kShade);
    // title plate with three claw slashes (the Cat Arena icon's motif)
    fill(96, 70, 448, 2, kBrassDim);
    fill(96, 168, 448, 2, kBrassDim);
    text_centered(92, "CAT ARENA", 32, kBrass);
    text_centered(132, "FERAL CLAW", 20, kText);
    for(int i = 0; i < 3; ++i) fill(470.0f + float(i) * 10, 84, 4, 70, kSlash);
    const auto list = items();
    for(int i = 0; i < int(list.size()); ++i) {
        const float y = kItemTop + float(i) * kItemStep;
        const bool sel = i == g.selected;
        if(sel) fill(160, y - 8, 320, 32, kBrassDim);
        text_centered(y, list[size_t(i)].label, kItemSize, sel ? kText : kBrass);
    }
    text_centered(452, CA_BuildString("ui"), 7, kBrassDim);
    // cursor
    fill(g.cursor_x - 1, g.cursor_y - 6, 2, 12, kText);
    fill(g.cursor_x - 6, g.cursor_y - 1, 12, 2, kText);
}

void activate(int index) {
    const auto list = items();
    if(index < 0 || index >= int(list.size())) return;
    const Item& item = list[size_t(index)];
    if(!item.command) {   // resume
        g.menu = Menu::none;
        set_catcher(get_catcher() & ~KEYCATCH_UI);
        return;
    }
    if(g.menu == Menu::ingame) {
        g.menu = Menu::none;
        set_catcher(get_catcher() & ~KEYCATCH_UI);
    }
    exec(item.command);
}

void key_event(int key, bool down) {
    if(!down || (key & kCharFlag)) return;
    const int n = int(items().size());
    if(n == 0) return;
    if(key == kUp || key == kWheelUp) g.selected = (g.selected + n - 1) % n;
    else if(key == kDown || key == kWheelDown) g.selected = (g.selected + 1) % n;
    else if(key == kEnter || key == kKpEnter) activate(g.selected);
    else if(key == kMouse1) activate(item_at(g.cursor_x, g.cursor_y));
    else if(key == kEscape && g.menu == Menu::ingame) activate(0);
}

void mouse_event(int dx, int dy) {
    g.cursor_x = std::fmax(0.0f, std::fmin(640.0f, g.cursor_x + float(dx) / g.scale_x));
    g.cursor_y = std::fmax(0.0f, std::fmin(480.0f, g.cursor_y + float(dy) / g.scale_y));
    if(const int i = item_at(g.cursor_x, g.cursor_y); i >= 0) g.selected = i;
}

void set_menu(int which) {
    g.selected = 0;
    switch(which) {
    case UIMENU_MAIN: g.menu = Menu::main; break;
    case UIMENU_INGAME: g.menu = Menu::ingame; break;
    default: g.menu = Menu::none; break;
    }
    if(g.menu == Menu::none) set_catcher(get_catcher() & ~KEYCATCH_UI);
    else set_catcher(KEYCATCH_UI);
}

void draw_connect_screen(bool overlay) {
    if(!overlay) fill_screen(kPlate);
    uiClientState_t cs{};
    get_clientstate(&cs);
    text_centered(180, "CAT ARENA", 32, kBrass);
    const char* what = cs.connState < CA_CONNECTED ? "CONNECTING" : cs.connState < CA_ACTIVE ? "LOADING THE ARENA" : "READY";
    text_centered(240, what, 16, kText);
    if(cs.servername[0]) text_centered(270, cs.servername, 8, kBrassDim);
    if(cs.messageString[0]) text_centered(290, cs.messageString, 8, kSlash);
}

void init() {
    glconfig_t gl{};
    get_glconfig(&gl);
    g.vid_w = gl.vidWidth > 0 ? gl.vidWidth : 640;
    g.vid_h = gl.vidHeight > 0 ? gl.vidHeight : 480;
    g.scale_y = float(g.vid_h) / 480.0f;
    g.scale_x = g.scale_y;   // keep 4:3 proportions, centred
    g.bias_x = (float(g.vid_w) - 640.0f * g.scale_x) / 2;
    g.white = register_shader("white");
    g.chars = register_shader("gfx/2d/bigchars");
    print(CA_BuildString("ui"));
    print("\n");
    CA_SelfTest(print);
}

}  // namespace

// q_shared.c (shared Quake III string helpers) reports through these, as in every game module.
extern "C" void QDECL Com_Printf(const char* fmt, ...) {
    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    if(g_syscall) print(text);
}
extern "C" void QDECL Com_Error(int, const char* fmt, ...) {
    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    if(g_syscall) g_syscall(UI_ERROR, text);
    std::abort();   // UI_ERROR does not return; never unwind into the engine
}

extern "C" Q_EXPORT void dllEntry(intptr_t(QDECL* syscallptr)(intptr_t arg, ...)) { g_syscall = syscallptr; }

extern "C" Q_EXPORT intptr_t vmMain(int command, int arg0, int arg1, [[maybe_unused]] int arg2, int, int, int, int, int, int, int, int, int) {
    try {
        switch(command) {
        case UI_GETAPIVERSION: return UI_API_VERSION;
        case UI_INIT: init(); return 0;
        case UI_SHUTDOWN: return 0;
        case UI_KEY_EVENT: key_event(arg0, arg1 != 0); return 0;
        case UI_MOUSE_EVENT: mouse_event(arg0, arg1); return 0;
        case UI_REFRESH:
            if(g.menu != Menu::none) draw_menu();
            return 0;
        case UI_IS_FULLSCREEN: return g.menu == Menu::main ? 1 : 0;
        case UI_SET_ACTIVE_MENU: set_menu(arg0); return 0;
        case UI_CONSOLE_COMMAND: return 0;
        case UI_DRAW_CONNECT_SCREEN: draw_connect_screen(arg0 != 0); return 0;
        case UI_HASUNIQUECDKEY: return 0;
        default: return -1;
        }
    } catch(...) {
        if(g_syscall) g_syscall(UI_PRINT, "^1Cat Arena ui: internal error caught at the module boundary\n");
        return -1;
    }
}
