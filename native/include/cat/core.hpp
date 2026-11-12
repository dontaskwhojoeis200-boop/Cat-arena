#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <utility>
#include "cat/utf8_path.hpp"

namespace cat {
void log(std::string_view level, std::string_view subsystem, std::string_view message);
// N6.5: also append every log line to this file (empty path: stop). Returns false if it cannot be opened.
bool set_log_file(const std::filesystem::path& path);

struct Settings {
    int width{1280}, height{720}, frame_cap{0};
    bool vsync{true}, validation{true};
    float sensitivity{0.002F};
    void validate() const;
    void write(std::ostream&) const;
    static Settings read(std::istream&);
    bool operator==(const Settings&) const = default;
};
struct Paths {
    std::filesystem::path assets, user;
    static Paths from(const std::filesystem::path& executable, const std::filesystem::path& user_dir);
};
// Z-up world -> right/up/back camera basis at zero yaw, looking along +Y.
struct Vec3 { float x{}, y{}, z{}; bool operator==(const Vec3&) const = default; };
constexpr Vec3 world_to_view(Vec3 v) { return {v.x, v.z, -v.y}; }
constexpr Vec3 view_to_world(Vec3 v) { return {v.x, -v.z, v.y}; }
constexpr Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
class Random {
    std::uint64_t state_;
public:
    explicit Random(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next(); // specified SplitMix64; independent of stdlib distribution implementation
    float unit(); // [0,1), 24-bit mantissa
};
// Integer rational phase: nanoseconds * 120. No truncated 8,333,333ns timestep drift.
class FixedClock {
    std::int64_t phase_{};
    std::uint64_t ticks_{};
    std::chrono::nanoseconds dropped_{};
public:
    static constexpr int hz=120;
    static constexpr double dt=1.0/hz;
    static constexpr int max_steps=7;
    unsigned advance(std::chrono::nanoseconds elapsed, const std::function<void(double)>& step);
    double alpha() const { return static_cast<double>(phase_)/1'000'000'000.0; }
    std::uint64_t ticks() const { return ticks_; }
    std::chrono::nanoseconds dropped() const { return dropped_; }
    void reset() { phase_=0; ticks_=0; dropped_={}; }
};
class Stopwatch {
    std::chrono::steady_clock::time_point last_{std::chrono::steady_clock::now()};
public:
    std::chrono::nanoseconds lap();
};
// Key and Action share positions: Input stores the button for key index k as action index k.
enum class Action : unsigned { forward, backward, left, right, jump, fire, pause, crouch, sprint, count };
enum class Key : unsigned { w, s, a, d, space, mouse_left, escape, left_ctrl, left_shift, count };
struct Button { bool held{}, pressed{}, released{}; };
class Input {
    std::array<Button, static_cast<unsigned>(Action::count)> buttons_{};
public:
    void set(Key, bool down);
    const Button& operator[](Action a) const { return buttons_.at(static_cast<unsigned>(a)); }
    void end_tick();
    void release_all();
};
// Move-only scope ownership, used for partial Vulkan initialization and SDL teardown.
template<class F> class ScopeExit {
    F fn_; bool active_{true};
public:
    explicit ScopeExit(F fn) : fn_(std::move(fn)) {}
    ScopeExit(const ScopeExit&)=delete;
    ScopeExit& operator=(const ScopeExit&)=delete;
    ScopeExit(ScopeExit&& other) noexcept : fn_(std::move(other.fn_)), active_(std::exchange(other.active_,false)) {}
    ~ScopeExit() noexcept { if(active_) fn_(); }
    void release() noexcept { active_=false; }
};
struct Snapshot { std::uint64_t tick{}; double interpolation{}; };
bool self_test();
}
