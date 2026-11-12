#include "cat/core.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
using namespace std::chrono_literals;
void require(bool value, const char* why) { if(!value) throw std::runtime_error(why); }
template<class F> void rejects(F f) { bool caught=false; try {f();} catch(const std::exception&) {caught=true;} require(caught,"expected rejection"); }
int main() {
 try {
    require(cat::self_test(),"self test");
    cat::FixedClock c; double time=0;
    for(int i=0;i<6000;++i) c.advance(10ms,[&](double dt){time+=dt;});
    require(c.ticks()==7200 && c.alpha()==0 && std::abs(time-60)<1e-8,"60 second drift");
    c.reset(); c.advance(4ms,[](double){}); require(std::abs(c.alpha()-.48)<1e-12,"interpolation");
    c.advance(-1s,[](double){throw std::runtime_error("negative tick");});
    require(std::abs(c.alpha()-.48)<1e-12,"negative time");
    auto count=c.advance(10s,[](double){});
    require(count<=7 && c.alpha()>=0 && c.alpha()<1 && c.dropped()>=9945ms,"stall bounded");
    cat::FixedClock a,b;
    for(int i=0;i<1000;++i) a.advance(1ms,[](double){});
    for(int i=0;i<40;++i) b.advance(25ms,[](double){});
    require(a.ticks()==b.ticks() && a.alpha()==b.alpha(),"partition invariance");
    std::cout<<"PASS clock: drift, interpolation, negative time, stall, partition invariance\n";
    cat::Settings settings; settings.vsync=false; settings.frame_cap=240;
    std::stringstream stream; settings.write(stream); require(cat::Settings::read(stream)==settings,"settings roundtrip");
    for(const char* bad:{"cat-settings 2", "cat-settings 1 0 720 0 1 1 .002", "cat-settings 1 1280 720 0 2 1 .002", "cat-settings 1 1280 720 0 1 1 .002 extra"})
        rejects([&]{std::istringstream s(bad); (void)cat::Settings::read(s);});
    std::cout<<"PASS settings: roundtrip and malformed/range/version rejection\n";
    cat::Vec3 v{2,3,4}; require(cat::view_to_world(cat::world_to_view(v))==v,"coordinate inverse");
    require(cat::world_to_view({0,1,0})==cat::Vec3{0,0,-1},"forward negative view Z");
    require(cat::cross({1,0,0},{0,1,0})==cat::Vec3{0,0,1},"right handed");
    std::cout<<"PASS math: Z-up right-handed basis and inverse\n";
    cat::Random r(123),s(123),other(124); require(r.next()!=other.next(),"different seed"); s.next();
    for(int i=0;i<10000;++i) {require(r.next()==s.next(),"seed replay");auto u=other.unit();require(u>=0&&u<1,"rng range");}
    std::cout<<"PASS rng: golden vector, replay and bounded unit samples\n";
    cat::Input input; input.set(cat::Key::w,true);input.set(cat::Key::w,true);
    require(input[cat::Action::forward].pressed&&input[cat::Action::forward].held,"press mapping");
    input.end_tick(); require(!input[cat::Action::forward].pressed&&input[cat::Action::forward].held,"edge consumed");
    input.release_all(); require(input[cat::Action::forward].released&&!input[cat::Action::forward].held,"focus loss");
    input.set(cat::Key::space,true);input.set(cat::Key::space,false);
    require(input[cat::Action::jump].pressed&&input[cat::Action::jump].released,"short tap retained");
    std::cout<<"PASS input: mapping, repeat, tick edges, short tap and focus loss\n";
    int destroyed=0;
    { cat::ScopeExit owner([&]{++destroyed;}); auto moved=std::move(owner); }
    require(destroyed==1,"move ownership");
    try {cat::ScopeExit owner([&]{++destroyed;}); throw 1;} catch(int) {}
    require(destroyed==2,"exception cleanup");
    {cat::ScopeExit owner([&]{++destroyed;});owner.release();}
    require(destroyed==2,"released ownership");
    std::cout<<"PASS ownership: single move cleanup, exception cleanup, release\n";
    const auto temp=std::filesystem::temp_directory_path()/("cat-n0-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    cat::ScopeExit cleanup([&]{std::error_code e;std::filesystem::remove_all(temp,e);});
    auto paths=cat::Paths::from("bin/cat_arena",temp);
    require(paths.assets.is_absolute()&&std::filesystem::is_directory(paths.user),"paths");
    rejects([]{cat::Paths::from("game",{});});
    std::cout<<"PASS paths: absolute assets and user directory creation\n";
    {   // N6.5: UTF-8 text <-> path. On Windows path(std::string) and path::string() use the ANSI code page;
        // the helpers must keep a folder such as "Zoë" (UTF-8 bytes 5A 6F C3 AB) intact, write and read through it.
        const std::string folder = "Zo\xC3\xAB \xE7\x8C\xAB", file = "caf\xC3\xA9.bin";
        const auto dir = temp / cat::utf8_path(folder);
        require(cat::path_utf8(dir.filename()) == folder, "utf8 folder name round trip");
        require(cat::path_utf8(cat::utf8_path(folder) / cat::utf8_path(file)) == folder + "/" + file || cat::path_utf8(cat::utf8_path(folder) / cat::utf8_path(file)) == folder + "\\" + file, "utf8 join");
        std::filesystem::create_directories(dir);
        { std::ofstream out(dir / cat::utf8_path(file), std::ios::binary); out << "cat"; require(bool(out), "write through a UTF-8 folder"); }
        std::ifstream in(dir / cat::utf8_path(file), std::ios::binary); std::string text; in >> text;
        require(text == "cat", "read through a UTF-8 folder");
        bool listed = false;
        for(const auto& entry : std::filesystem::directory_iterator(dir)) listed = listed || cat::path_utf8(entry.path().filename()) == file;
        require(listed, "UTF-8 file name listed back");
    }
    std::cout<<"PASS utf8 paths: non-ASCII folder and file names round trip, write, read and list\n";
    return 0;
 } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
