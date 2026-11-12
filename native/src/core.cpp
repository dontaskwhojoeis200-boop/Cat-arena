#include "cat/core.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace cat {
namespace {
std::mutex log_mutex;
std::ofstream log_file;
}
bool set_log_file(const std::filesystem::path& path) {
    std::lock_guard lock(log_mutex);
    if(log_file.is_open()) log_file.close();
    if(path.empty()) return true;
    log_file.open(path, std::ios::out | std::ios::trunc);
    return log_file.is_open();
}
void log(std::string_view level, std::string_view subsystem, std::string_view message) {
    std::lock_guard lock(log_mutex);
    // std::quoted escapes embedded quotation marks and backslashes; newlines flattened.
    std::string clean(message);
    std::replace(clean.begin(),clean.end(),'\n',' ');
    std::replace(clean.begin(),clean.end(),'\r',' ');
    std::clog << "level=" << std::quoted(std::string(level)) << " subsystem=" << std::quoted(std::string(subsystem))
              << " message=" << std::quoted(clean) << '\n';
    if(log_file.is_open()) log_file << "level=" << std::quoted(std::string(level)) << " subsystem=" << std::quoted(std::string(subsystem)) << " message=" << std::quoted(clean) << std::endl;
}
void Settings::validate() const {
    if(width<320 || width>16384 || height<200 || height>16384 || frame_cap<0 || frame_cap>1000 ||
       !std::isfinite(sensitivity) || sensitivity<=0 || sensitivity>1)
        throw std::runtime_error("Settings value outside supported range");
}
void Settings::write(std::ostream& out) const {
    validate();
    out << "cat-settings 1\n" << width << ' ' << height << ' ' << frame_cap << ' '
        << (vsync?1:0) << ' ' << (validation?1:0) << ' ' << std::setprecision(9) << sensitivity << '\n';
    if(!out) throw std::runtime_error("Cannot write settings");
}
Settings Settings::read(std::istream& in) {
    Settings s; std::string magic, tail; int version{}, vs{}, val{};
    if(!(in>>magic>>version) || magic!="cat-settings" || version!=1 ||
       !(in>>s.width>>s.height>>s.frame_cap>>vs>>val>>s.sensitivity) ||
       (vs!=0 && vs!=1) || (val!=0 && val!=1) || (in>>tail))
        throw std::runtime_error("Malformed or unsupported settings");
    s.vsync=vs==1; s.validation=val==1; s.validate(); return s;
}
Paths Paths::from(const std::filesystem::path& executable, const std::filesystem::path& user_dir) {
    if(user_dir.empty()) throw std::runtime_error("Empty user directory");
    std::filesystem::create_directories(user_dir);
    return {std::filesystem::absolute(executable).parent_path()/"assets", std::filesystem::absolute(user_dir)};
}
std::uint64_t Random::next() {
    auto z=(state_+=UINT64_C(0x9e3779b97f4a7c15));
    z=(z^(z>>30))*UINT64_C(0xbf58476d1ce4e5b9);
    z=(z^(z>>27))*UINT64_C(0x94d049bb133111eb);
    return z^(z>>31);
}
float Random::unit() { return static_cast<float>(next()>>40)*(1.0F/16777216.0F); }
unsigned FixedClock::advance(std::chrono::nanoseconds elapsed, const std::function<void(double)>& step) {
    // Match reference .055s frame and .06s accumulator bounds. Negative time is ignored.
    const auto ns=std::max<std::int64_t>(0,elapsed.count());
    const auto accepted=std::min<std::int64_t>(ns,55'000'000);
    dropped_+=std::chrono::nanoseconds(ns-accepted);
    const auto total=phase_+accepted*hz;
    constexpr std::int64_t limit=60'000'000LL*hz;
    if(total>limit) dropped_+=std::chrono::nanoseconds((total-limit)/hz);
    phase_=std::min(total,limit);
    unsigned steps=0;
    while(phase_>=1'000'000'000 && steps<max_steps) {
        step(dt); phase_-=1'000'000'000; ++ticks_; ++steps;
    }
    return steps;
}
std::chrono::nanoseconds Stopwatch::lap() {
    auto now=std::chrono::steady_clock::now(); auto d=now-last_; last_=now;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(d);
}
void Input::set(Key key, bool down) {
    const auto index=static_cast<unsigned>(key);
    if(index>=buttons_.size()) return;
    auto& b=buttons_[index];
    if(down!=b.held) { if(down) b.pressed=true; else b.released=true; b.held=down; }
}
void Input::end_tick() { for(auto& b:buttons_) { b.pressed=false; b.released=false; } }
void Input::release_all() { for(unsigned i=0;i<buttons_.size();++i) set(static_cast<Key>(i),false); }
bool self_test() {
    FixedClock clock; unsigned calls=0;
    for(int i=0;i<1000;++i) clock.advance(std::chrono::milliseconds(1),[&](double){++calls;});
    return calls==120 && clock.ticks()==120 && clock.alpha()==0 && Random(0).next()==UINT64_C(0xe220a8397b1dcdaf);
}
}
