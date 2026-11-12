// N5 HUD tests. GPU-free.
#include "cat/hud.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace cat;
namespace {
int checks = 0;
void check(bool value, const std::string& why) { ++checks; if(!value) throw std::runtime_error(why); }
}
int main() {
    try {
        for(const auto& w : combat::weapons()) for(const char c : std::string(w.name)) check(hud::has_glyph(c), std::string("font covers weapon name ") + w.name);
        for(const char c : std::string("0123456789 /.:-!HEALTH ELIMS DEATHS RELOADING PRACTICE TARGETS FRAG NEW WEAPON YOU DIED RESPAWNED SPAWN PROTECTION Q THE ARSENAL HELIOS CRUCIBLE BASTION KEEP REACTOR")) check(hud::has_glyph(c), "font covers HUD text");
        check(!hud::has_glyph('\x7f') && hud::has_glyph('a'), "lower case maps to upper case; unknown characters are reported");
        check(hud::Canvas::text_width("", 2) == 0 && hud::Canvas::text_width("A", 2) == 10 && hud::Canvas::text_width("AB", 3) == 33, "text width: 5 dots per glyph, 1 dot spacing");
        hud::Canvas c(200, 100);
        c.rect(50, 25, 100, 50, {1, 0, 0, .5F});
        check(c.vertices().size() == 6 && c.vertices()[0].x == -.5F && c.vertices()[0].y == -.5F && c.vertices()[2].x == .5F && c.vertices()[2].y == .5F && c.vertices()[0].a == .5F, "pixel rectangle -> normalized device coordinates");
        c.clear();
        const double end = c.text(10, 10, 2, "1", {});
        check(end == 22 && c.vertices().size() == 7 * 6, "glyph '1': seven horizontal runs, cursor advances 6 dots");
        c.clear();
        c.text(0, 0, 1, "-", {});
        check(c.vertices().size() == 6, "runs of lit dots are merged into one rectangle");
        for(const auto& size : {std::pair{1280.0, 720.0}, std::pair{640.0, 360.0}, std::pair{2560.0, 1440.0}, std::pair{800.0, 1000.0}}) {
            hud::Canvas k(size.first, size.second);
            hud::State s;
            s.health = 37; s.ammo = 5; s.clip = 8; s.weapon = "QUAD SHOTGUN"; s.other_weapon = "GRENADE LAUNCHER"; s.map_title = "THE ARSENAL"; s.frags = 12; s.deaths = 3; s.targets = 4;
            s.hit_marker = 1; s.damage_flash = .6; s.notice = "FRAG - NEW WEAPON: RIFT CANNON"; s.notice_alpha = 1; s.respawn_grace = 1;
            hud::build(k, s);
            const auto idle = k.vertices().size();
            check(idle > 600 && idle % 3 == 0 && idle < 90000, "HUD is a triangle list of reasonable size: " + std::to_string(idle));
            for(const auto& v : k.vertices()) check(std::fabs(v.x) <= 1.0001F && std::fabs(v.y) <= 1.0001F && v.a >= 0 && v.a <= 1, "HUD stays on screen");
            hud::Canvas r(size.first, size.second);
            s.reloading = .5; s.reload_time = 2.5;
            hud::build(r, s);
            check(r.vertices().size() != idle, "the reload state changes the HUD");
        }
        std::vector<hud::SolidVertex> boxes;
        combat::Target t; t.x = 5; t.y = 6; t.z = 1; t.hp = 50; t.max_hp = 100;
        hud::target_boxes({t, t}, boxes);
        check(boxes.size() == 2 * 2 * 36, "two boxes (body, head) per target");
        float zmax = 0;
        for(const auto& v : boxes) zmax = std::max(zmax, v.z);
        check(std::fabs(zmax - 3.18F) < 1e-5F, "boxes are the legacy hit boxes");
        std::cout << "PASS hud tests: " << checks << " checks\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
