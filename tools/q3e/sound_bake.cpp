// N10-Q3E stage D part 6: renders Cat Arena's sounds for Quake3e's sound system.
//
// Quake III plays a sample at a fixed volume (s_volume, distance and stereo position only): no
// per-call gain, playback rate, filters or buses. N10 (the legacy rules in native/src/audio_game.cpp)
// gives every sound a gain, a bus level, sometimes a rate and biquad filters. This tool renders each
// sound through N10's own mixer (native/src/audio_mixer.cpp: Web Audio-matched interpolation,
// biquads, oscillators) with those values baked in, scaled so that Quake III's mixer reproduces
// N10's output level:
//   local sound (the player's own: guns, explosions, body, beeps): Quake III output = sample x 127 x
//     (255 x s_volume) / 65536 per channel; N10 = sample x gain x bus x master on both channels.
//     With s_volume = master: baked = sample x gain x bus x 65536 / (127 x 255).
//   enemy voice (positional): Quake III gives 63.5 per channel straight ahead; N10's equal-power
//     panner gives 0.7071. baked = sample x gain x bus x 0.7071 x 65536 / (63.5 x 255).
// Distance attenuation and pan of the voices are left to Quake III (see docs/q3e/TEST_MATRIX.md).
// Per-play random pitch (N10: +-1.4 %) is not reproduced; the voice families' fixed tone is.
//
//   sound_bake <N10 sample dir (assets/n7/audio/sfx)> <out dir>   writes <out>/<name>.wav + sounds.txt
//
// GPL v2 or later (docs/q3e/LICENSING.md).
#include "cat/audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

using namespace cat::audio;

namespace {
constexpr int kRate = 44100;
constexpr double kLocal = 65536.0 / (127.0 * 255.0);
constexpr double kVoice = 0.70710678118654752 * 65536.0 / (63.5 * 255.0);
constexpr double kWeaponBus = .85, kEnemyBus = .80;   // MixSettings defaults (weapons 85, enemies 80)
constexpr double kPi = 3.141592653589793;

std::map<std::string, std::shared_ptr<const Buffer>> load_bank(const std::filesystem::path& dir) {   // SampleBank::load
    std::ifstream list(dir / "bank.txt");
    if(!list) throw std::runtime_error("missing " + (dir / "bank.txt").string());
    std::map<std::string, std::shared_ptr<const Buffer>> bank;
    std::string line;
    while(std::getline(list, line)) {
        if(line.empty()) continue;
        std::istringstream row(line);
        std::string key, file, length;
        std::getline(row, key, '\t');
        std::getline(row, file, '\t');
        Buffer b = decode_ogg(read_file(dir / file));
        if(std::getline(row, length, '\t') && !length.empty()) {
            const auto frames = static_cast<std::size_t>(std::stoul(length));
            if(frames < b.frames()) b.samples.resize(frames * static_cast<std::size_t>(b.channels));
        }
        bank[key] = std::make_shared<const Buffer>(resample(b, kRate));
    }
    return bank;
}

// GameAudio::synth_body for the footstep (the bank has no step sample); seeded, so the step is the
// same in every build (N10 draws it once per session from its sound random stream).
std::shared_ptr<const Buffer> synth_step() {
    std::uint32_t state = 0x5eed;
    auto rnd = [&] {   // mulberry32
        std::uint32_t t = (state += 0x6D2B79F5u);
        t = (t ^ (t >> 15)) * (t | 1u);
        t ^= t + (t ^ (t >> 7)) * (t | 61u);
        return double(t ^ (t >> 14)) / 4294967296.0;
    };
    const double duration = .17;
    const auto n = static_cast<std::size_t>(std::floor(kRate * duration));
    Buffer b;
    b.rate = kRate; b.channels = 1; b.samples.resize(n);
    double low = 0, mid = 0, phase = 0;
    for(std::size_t i = 0; i < n; ++i) {
        const double t = double(i) / kRate, w = rnd() * 2 - 1;
        low += (w - low) * .034; mid += (w - mid) * .23;
        phase += 2 * kPi * (90 - 60 * t / duration) / kRate;
        const double v = (w * .20 + mid * .65 + std::sin(phase) * .50) * std::exp(-t * 31);
        b.samples[i] = static_cast<float>(std::tanh(v) * std::min(1.0, t * 1500) * std::min(1.0, (duration - t) * 30));
    }
    return std::make_shared<const Buffer>(std::move(b));
}

struct Out { std::string name; std::size_t frames; double peak; };
std::vector<Out> written;
std::filesystem::path out_dir;

void write_wav(const std::string& name, const std::vector<float>& mono) {
    std::vector<std::int16_t> pcm(mono.size());
    double peak = 0;
    for(std::size_t i = 0; i < mono.size(); ++i) {
        peak = std::max(peak, double(std::fabs(mono[i])));
        pcm[i] = static_cast<std::int16_t>(std::lround(std::clamp(double(mono[i]), -1.0, 1.0) * 32767));
    }
    std::ofstream f(out_dir / (name + ".wav"), std::ios::binary);
    auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const std::uint32_t bytes = static_cast<std::uint32_t>(pcm.size() * 2);
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(1); u32(kRate); u32(kRate * 2); u16(2); u16(16);
    f.write("data", 4); u32(bytes); f.write(reinterpret_cast<const char*>(pcm.data()), bytes);
    written.push_back({name, mono.size(), peak});
}

// Renders one voice or tone alone through N10's mixer (master 1, buses 1, compressor off) to mono.
template<class Start> void render(const std::string& name, Start start) {
    Mixer m(kRate);
    m.set_compressor(false);
    start(m);
    std::vector<float> mono, block(2 * 512);
    while(m.voices() > 0 && mono.size() < std::size_t(kRate) * 10) {
        std::fill(block.begin(), block.end(), 0.f);
        m.render(block.data(), 512);
        for(std::size_t i = 0; i < 512; ++i) mono.push_back(.5f * (block[2 * i] + block[2 * i + 1]));
    }
    while(!mono.empty() && std::fabs(mono.back()) < 1.0f / 65536) mono.pop_back();   // trailing silence (filter tails)
    write_wav(name, mono);
}
void sample(const std::string& name, std::shared_ptr<const Buffer> buf, double gain, double rate = 1, std::vector<Filter> filters = {}) {
    if(!buf) throw std::runtime_error("missing sample for " + name);
    render(name, [&](Mixer& m) { m.play({buf, Bus::weapon, gain, rate, std::nullopt, std::move(filters), 0}); });
}
void beep(const std::string& name, double f, double len, Wave wave, double volume) {   // GameAudio::beep
    render(name, [&](Mixer& m) { m.tone({wave, f, std::max(45.0, f * .49), volume * kWeaponBus * kLocal, .001 * kWeaponBus * kLocal, len, Bus::weapon}); });
}
}  // namespace

int main(int argc, char** argv) {
    if(argc != 3) { std::fprintf(stderr, "usage: sound_bake <assets/n7/audio/sfx> <out dir>\n"); return 2; }
    try {
        const auto bank = load_bank(argv[1]);
        if(bank.size() != 48) throw std::runtime_error("expected the 48 N10 samples, found " + std::to_string(bank.size()));
        out_dir = argv[2];
        std::filesystem::create_directories(out_dir);
        // playGunSound: two takes per weapon; level heavy .50, rapid .38, else .42
        for(int i = 0; i < 16; ++i) {
            const bool heavy = i == 1 || i == 4 || i == 7 || i == 12 || i == 15, rapid = i == 0 || i == 5 || i == 11 || i == 14;
            const double level = heavy ? .50 : rapid ? .38 : .42;
            for(int t = 0; t < 2; ++t) sample("gun" + std::to_string(i) + "_" + std::to_string(t), bank.at("gun" + std::to_string(i) + ":" + std::to_string(t)), level * kWeaponBus * kLocal);
        }
        // playExplosionSound(power): min(.70, .34 power); rocket 1.15, grenade 1.08, rift 1.42
        for(const auto& [name, power] : std::vector<std::pair<std::string, double>>{{"rocket", 1.15}, {"grenade", 1.08}, {"rift", 1.42}})
            sample("explosion_" + name, bank.at("explosion"), std::min(.70, .34 * power) * kWeaponBus * kLocal);
        // playBodySound(kind, force): samples at .16 force (.24 landing); the step is synthesised at .12 force
        sample("body_jump", bank.at("body:jump"), .16 * kWeaponBus * kLocal);
        const double slide[] = {.6, .9, 1.2}, land[] = {.3, .6, .95, 1.3};
        for(int k = 0; k < 3; ++k) sample("body_slide_" + std::to_string(k), bank.at("body:slide"), .16 * slide[k] * kWeaponBus * kLocal);
        for(int k = 0; k < 4; ++k) sample("body_land_" + std::to_string(k), bank.at("body:land"), .24 * land[k] * kWeaponBus * kLocal);
        const auto step = synth_step();
        sample("body_step_walk", step, .12 * .48 * kWeaponBus * kLocal);
        sample("body_step_sprint", step, .12 * .60 * kWeaponBus * kLocal);
        // playEnemyVoice: three takes per kind; family tone and filters; base level per kind
        struct Family { const char* name; double tone, lowpass, highpass; };
        const Family families[] = {{"normal", 1.0, 4100, 155}, {"bass", .92, 3700, 130}, {"light", 1.025, 4600, 155}};
        const char* kinds[] = {"idle", "attack", "hurt", "eliminated"};
        const double base[] = {.31, .66, .60, .74};
        for(const auto& fam : families) for(int k = 0; k < 4; ++k) for(int t = 0; t < 3; ++t)
            sample(std::string("voice_") + fam.name + "_" + kinds[k] + "_" + std::to_string(t), bank.at(std::string("cat:") + kinds[k] + ":" + std::to_string(t)),
                   base[k] * kEnemyBus * kVoice, fam.tone, {Filter{FilterType::lowpass, fam.lowpass, 1}, Filter{FilterType::highpass, fam.highpass, 1}});
        // beeps: switchWeapon, reloadWeapon, takeDamage, medkit pickup, TEST AUDIO
        beep("beep_switch", 700, .07, Wave::sine, .015);
        beep("beep_reload", 350, .08, Wave::triangle, .019);
        beep("beep_hurt", 100, .08, Wave::sawtooth, .013);
        beep("beep_medkit", 830, .14, Wave::sine, .022);
        beep("beep_test", 410, .14, Wave::sawtooth, .08);
        std::ofstream list(out_dir / "sounds.txt");
        int clipped = 0;
        for(const auto& o : written) {
            list << o.name << '\t' << o.frames << '\t' << o.peak << '\n';
            if(o.peak > 1) { std::fprintf(stderr, "clipped: %s (peak %.3f)\n", o.name.c_str(), o.peak); ++clipped; }
        }
        std::printf("%zu sounds written to %s, %d clipped\n", written.size(), out_dir.string().c_str(), clipped);
        return clipped ? 1 : 0;
    } catch(const std::exception& e) {
        std::fprintf(stderr, "sound_bake: %s\n", e.what());
        return 1;
    }
}
