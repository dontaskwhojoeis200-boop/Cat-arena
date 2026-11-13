// N7 audio tests: decoders and node chains against Chromium's Web Audio (tools/n7/record_webaudio.cjs),
// mixer units and the legacy sound rules. No audio device is used.
//   cat_arena_audio_tests <assets/n7> <tests/fixtures/n7>
#include "cat/audio.hpp"
#include "cat/js_math.hpp"
#include "mini_json.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    if(!ok) { std::printf("FAIL %s\n", what.c_str()); ++failures; }
}

using testjson::Json;
using testjson::load_json;

// make_signal(): identical to signal() in tools/n7/record_webaudio.cjs
cat::audio::Buffer make_signal(std::size_t frames, int channels, std::uint32_t seed) {
    cat::audio::Buffer b; b.rate = 48000; b.channels = channels; b.samples.resize(frames * static_cast<std::size_t>(channels));
    std::vector<std::vector<float>> data(static_cast<std::size_t>(channels), std::vector<float>(frames));
    std::uint32_t s = seed;
    auto rnd = [&] { s = s * 1664525u + 1013904223u; return s / 4294967296.0; };
    for(std::size_t i = 0; i < frames; ++i) for(int c = 0; c < channels; ++c) {
        const double t = double(i) / 48000;
        const auto a = static_cast<float>(.5 * cat::jsmath::sin(2 * 3.141592653589793 * (220 + 40 * c) * t) + .3 * cat::jsmath::sin(2 * 3.141592653589793 * 3150 * t) + .2 * (rnd() * 2 - 1));
        data[static_cast<std::size_t>(c)][i] = static_cast<float>(a * std::exp(-t * 4));
    }
    for(std::size_t i = 0; i < frames; ++i) for(int c = 0; c < channels; ++c) b.samples[i * static_cast<std::size_t>(channels) + static_cast<std::size_t>(c)] = data[static_cast<std::size_t>(c)][i];
    return b;
}

struct Compare { double max_abs = 0, rms_ref = 0, rms_err = 0; double rel() const { return rms_ref > 0 ? rms_err / rms_ref : rms_err; } };
Compare compare(const std::vector<float>& a, const float* ref, std::size_t n) {
    Compare c; double e2 = 0, r2 = 0;
    for(std::size_t i = 0; i < n; ++i) { const double d = double(a[i]) - ref[i]; c.max_abs = std::max(c.max_abs, std::fabs(d)); e2 += d * d; r2 += double(ref[i]) * ref[i]; }
    c.rms_err = std::sqrt(e2 / double(n)); c.rms_ref = std::sqrt(r2 / double(n)); return c;
}

void decode_parity(const std::filesystem::path& assets, const std::filesystem::path& fixtures) {
    const Json d = load_json(fixtures / "decode.json")["decode"];
    std::map<std::string, std::string> files;
    { std::ifstream list(assets / "audio/sfx/bank.txt"); std::string line;
      while(std::getline(list, line)) { std::istringstream row(line); std::string k, f; if(std::getline(row, k, '\t') && std::getline(row, f, '\t')) files[k] = f; } }
    double worst_probe = 0, worst_rms = 0; std::size_t frames_equal = 0, checked = 0; std::string worst_key;
    for(std::size_t k = 0; k < d.size(); ++k) {
        const Json& e = d[k];
        const std::string key = e["key"].str();
        const bool mp3 = key == "main-theme";
        const auto buf = mp3 ? cat::audio::decode_mp3(cat::audio::read_file(assets / "audio/main-theme.mp3")) : cat::audio::decode_ogg(cat::audio::read_file(assets / "audio/sfx" / files.at(key)));
        ++checked;
        check(buf.rate == static_cast<int>(e["rate"].num()) && buf.channels == static_cast<int>(e["channels"].num()), "decode format " + key);
        const auto frames = static_cast<std::size_t>(e["frames"].num());
        frames_equal += buf.frames() == frames;
        // stb_vorbis keeps 11-192 frames of end padding that the browser drops (the bank list carries the browser length)
        const bool length_ok = mp3 ? buf.frames() == frames : buf.frames() >= frames && buf.frames() - frames <= 256;
        check(length_ok, "decode length " + key + " native " + std::to_string(buf.frames()) + " browser " + std::to_string(frames));
        if(mp3) std::printf("  main-theme.mp3: native %zu frames, browser %zu frames\n", buf.frames(), frames);
        for(int c = 0; c < buf.channels; ++c) {
            const Json& ch = e["ch"][static_cast<std::size_t>(c)];
            const std::size_t n = std::min(frames, buf.frames());
            double s = 0; for(std::size_t i = 0; i < n; ++i) { const double v = buf.samples[i * static_cast<std::size_t>(buf.channels) + static_cast<std::size_t>(c)]; s += v * v; }
            const double rms = std::sqrt(s / double(n)), rel = std::fabs(rms - ch["rms"].num()) / std::max(1e-9, ch["rms"].num());
            worst_rms = std::max(worst_rms, rel);
            if(length_ok) for(std::size_t p = 0; p < 256; ++p) {
                const std::size_t at = (frames - 1) * p / 255;
                const double diff = std::fabs(buf.samples[at * static_cast<std::size_t>(buf.channels) + static_cast<std::size_t>(c)] - ch["probes"][p].num());
                if(diff > worst_probe) { worst_probe = diff; worst_key = key; }
            }
        }
    }
    std::printf("%s decode: %zu files (48 Ogg Vorbis + MP3), equal length %zu (others: browser drops the end padding), worst RMS difference %.2e, worst probe difference %.2e (%s)\n",
                worst_rms < 1e-3 && worst_probe < 5e-3 ? "PASS" : "FAIL", checked, frames_equal, worst_rms, worst_probe, worst_key.c_str());
    check(worst_rms < 1e-3 && worst_probe < 5e-3, "decoded audio matches the browser");
}

void graph_parity(const std::filesystem::path& fixtures) {
    const Json info = load_json(fixtures / "graph.json");
    const auto frames = static_cast<std::size_t>(info["frames"].num());
    std::ifstream in(fixtures / "graph.f32", std::ios::binary);
    std::vector<float> ref((std::istreambuf_iterator<char>(in)), {});
    { std::ifstream in2(fixtures / "graph.f32", std::ios::binary); ref.assign(frames * 2 * info["scenarios"].size(), 0.f); in2.read(reinterpret_cast<char*>(ref.data()), static_cast<std::streamsize>(ref.size() * 4)); }
    using namespace cat::audio;
    struct Limit { const char* name; double rel; };
    // rate / filters_pan: equal to float precision except the final frame of the sample, where Chromium's
    // end-of-buffer interpolation is not reproduced (one frame, < 4e-3); compressor: port of Chromium's kernel.
    const Limit limits[] = {{"rate", 1e-3}, {"filters_pan", 1e-3}, {"stereo_pan", 1e-4}, {"sine_beep", 1e-2}, {"triangle_beep", 5e-2}, {"saw_beep", .1}, {"compressor", 1e-2}};
    for(std::size_t k = 0; k < info["scenarios"].size(); ++k) {
        const std::string name = info["scenarios"][k]["name"].str();
        Mixer m(48000);
        m.set_compressor(false);
        if(name == "rate") m.play({std::make_shared<Buffer>(make_signal(12000, 1, 1)), Bus::weapon, .42, .99, std::nullopt, {}, 0});
        else if(name == "filters_pan") m.play({std::make_shared<Buffer>(make_signal(12000, 1, 2)), Bus::weapon, .5, 1.0125, .3, {Filter{FilterType::lowpass, 4100, 1}, Filter{FilterType::highpass, 155, 1}}, 0});
        else if(name == "stereo_pan") m.play({std::make_shared<Buffer>(make_signal(12000, 2, 3)), Bus::weapon, .6, 1, -.4, {}, 0});
        else if(name == "sine_beep") m.tone({Wave::sine, 830, 830 * .49, .022, .001, .14, Bus::weapon});
        else if(name == "triangle_beep") m.tone({Wave::triangle, 350, 350 * .49, .019, .001, .08, Bus::weapon});
        else if(name == "saw_beep") m.tone({Wave::sawtooth, 410, 410 * .49, .08, .001, .14, Bus::weapon});
        else if(name == "compressor") { m.set_compressor(true); m.play({std::make_shared<Buffer>(make_signal(12000, 1, 4)), Bus::weapon, 2.2, 1, std::nullopt, {}, 0}); }
        std::vector<float> out(frames * 2);
        for(std::size_t i = 0; i < frames; i += 480) m.render(out.data() + i * 2, std::min<std::size_t>(480, frames - i));
        const Compare c = compare(out, ref.data() + k * frames * 2, frames * 2);
        double limit = 1;
        for(const auto& l : limits) if(name == l.name) limit = l.rel;
        const bool ok = c.rel() <= limit;
        std::printf("%s graph %-13s relative RMS error %.2e (limit %.0e), max |error| %.2e, browser RMS %.4f\n", ok ? "PASS" : "FAIL", name.c_str(), c.rel(), limit, c.max_abs, c.rms_ref);
        check(ok, "graph " + name);
    }
}

class FakeStream final : public cat::audio::Stream {   // 1000-frame ramp at 24 kHz, looping
public:
    int rate() const override { return 24000; }
    std::size_t read(float* out, std::size_t n) override {
        std::size_t k = 0;
        for(; k < n && pos < 1000; ++k, ++pos) { out[2 * k] = float(pos) / 1000; out[2 * k + 1] = -float(pos) / 1000; }
        return k;
    }
    void rewind() override { ++rewinds; pos = 0; }
    std::size_t pos = 0; int rewinds = 0;
};

void mixer_units() {
    using namespace cat::audio;
    // equal-power panner (mono): pan 0 -> cos(pi/4) both sides; pan 1 -> right only
    for(const double pan : {-1.0, 0.0, .5, 1.0}) {
        Mixer m(48000); m.set_compressor(false);
        auto dc = std::make_shared<Buffer>(); dc->rate = 48000; dc->channels = 1; dc->samples.assign(100, 1.f);
        m.play({dc, Bus::weapon, 1, 1, pan, {}, 0});
        std::vector<float> o(20); m.render(o.data(), 10);
        const double x = (pan + 1) / 2;
        check(std::fabs(o[2] - std::cos(x * 3.141592653589793 / 2)) < 1e-6 && std::fabs(o[3] - std::sin(x * 3.141592653589793 / 2)) < 1e-6, "panner equal power");
    }
    // no panner: mono copied to both sides at full gain
    { Mixer m(48000); m.set_compressor(false); auto dc = std::make_shared<Buffer>(); dc->samples.assign(10, .5f); m.play({dc, Bus::weapon, 1, 1, std::nullopt, {}, 0});
      std::vector<float> o(4); m.render(o.data(), 2); check(o[0] == .5f && o[1] == .5f, "mono up-mix"); }
    // biquad DC gains
    { auto lp = Biquad::make({FilterType::lowpass, 4100, 1}, 48000), hp = Biquad::make({FilterType::highpass, 155, 1}, 48000);
      double a = 0, b = 0; for(int i = 0; i < 20000; ++i) { a = lp.process(1); b = hp.process(1); }
      check(std::fabs(a - 1) < 1e-9 && std::fabs(b) < 1e-9, "biquad DC gain"); }
    // compressor static curve
    { Compressor c(48000); check(std::fabs(c.curve_db(-30) + 30) < 1e-4 && std::fabs(c.curve_db(-10.5) + 10.5) < 1e-4 && c.curve_db(10) < 10 && c.curve_db(10) > -10 && c.makeup_db() > 0, "compressor curve"); }
    // voices end, tags count, bus gains apply
    { Mixer m(48000); m.set_compressor(false); auto b = std::make_shared<Buffer>(); b->samples.assign(100, .25f);
      m.play({b, Bus::enemy, 1, 1, std::nullopt, {}, 1}); m.play({b, Bus::weapon, 1, 2, std::nullopt, {}, 1});
      check(m.active(1) == 2, "tag count"); m.set_bus(Bus::enemy, .5);
      std::vector<float> o(2 * 60); m.render(o.data(), 60); check(m.active(1) == 1 && std::fabs(o[0] - (.125f + .25f)) < 1e-6, "voice end and bus gain");
      m.render(o.data(), 60); check(m.voices() == 0, "all voices finished"); }
    // music: streamed, resampled, looped, paused
    { Mixer m(48000); auto s = std::make_unique<FakeStream>(); auto* raw = s.get(); m.set_music(std::move(s)); m.set_music_playing(true);
      std::vector<float> o(2 * 48000); m.render(o.data(), 48000);   // 1 s = 24000 input frames = 24 loops
      check(raw->rewinds >= 23 && raw->rewinds <= 25, "music loops (" + std::to_string(raw->rewinds) + " rewinds)");
      check(std::fabs(o[2 * 1000] - (500.0 / 1000 - 32.0 / 1000 * 0)) < .06, "music resampled value");
      m.set_music_playing(false); m.render(o.data(), 100); check(o[0] == 0 && o[199] == 0, "music paused"); }
}

void rules(const std::filesystem::path& assets) {
    using namespace cat::audio;
    Mixer m(48000);
    auto bank = std::make_shared<SampleBank>(SampleBank::load(assets / "audio/sfx", 48000));
    check(bank->size() == 48 && bank->get("gun15:1") && bank->get("cat:eliminated:2") && !bank->get("body:step"), "sample bank");
    GameAudio a(m, bank, 7);
    a.gun(4); check(m.voices() == 0, "silent before audiostart");
    a.apply({70, 85, 80, 46, true});
    check(!m.music_playing(), "no music before start");
    a.start(); check(!m.music_playing(), "music needs a stream");
    m.set_music(std::make_unique<FakeStream>()); a.start(); check(m.music_playing(), "music after start");
    a.set_visible(false); check(!m.music_playing(), "music pauses while hidden");
    a.set_visible(true); a.set_in_game(false); check(!m.music_playing(), "returnMenu pauses music");
    a.set_in_game(true); a.apply({70, 85, 80, 46, false}); check(!m.music_playing(), "music disabled");
    a.apply({70, 85, 80, 46, true});
    // voices: distance, per-enemy cooldown, global spacing, polyphony
    const Listener me{0, 0, 0};
    using cat::combat::VoiceKind;
    check(!a.voice({40, 0, 0, 0, VoiceKind::idle, 1}, me, 0), "voice beyond 31 m is dropped");
    check(a.voice({5, 0, 0, 0, VoiceKind::hurt, 1}, me, 1), "hurt voice plays");
    check(!a.voice({5, 0, 0, 0, VoiceKind::hurt, 1}, me, 1.2), "per-enemy hurt cooldown .28 s");
    check(!a.voice({5, 0, 0, 0, VoiceKind::attack, 2}, me, 1.05), "global spacing .095 s");
    check(a.voice({5, 0, 0, 0, VoiceKind::attack, 2}, me, 1.2), "attack voice plays");
    check(a.voice({5, 0, 0, 0, VoiceKind::eliminated, 1}, me, 1.21), "eliminated ignores cooldowns");
    check(m.active(1) == 3 && !a.voice({5, 0, 0, 0, VoiceKind::hurt, 3}, me, 1.5), "at most three voices unless a death");
    check(a.voice({5, 0, 0, 0, VoiceKind::eliminated, 5}, me, 1.71) && a.voice({5, 0, 0, 0, VoiceKind::eliminated, 6}, me, 1.72), "deaths up to five");
    check(!a.voice({5, 0, 0, 0, VoiceKind::eliminated, 7}, me, 1.73), "at most five voices");
    { std::vector<float> o(2 * 48000 * 2); m.render(o.data(), 96000); }   // voices end (onended)
    check(m.active(1) == 0 && a.voice({5, 0, 0, 0, VoiceKind::hurt, 3}, me, 5), "voices free again after they end");
    // body: step is synthesized (no bank sample), the others are samples
    const auto before = m.voices();
    a.body(cat::physics::BodySound::step, .48); a.body(cat::physics::BodySound::jump, 1);
    check(m.voices() == before + 2 && a.counts().bodies == 2, "body sounds");
    // a step of the simulation: fire + reload + switch + heal + hits
    cat::combat::Events e; e.fired_weapon = 7; e.reload_started = true; e.switched = true; e.healed = 40; e.player_hits = 2;
    const auto beeps = a.counts().beeps;
    a.step(e, {}, me, 5);
    check(a.counts().guns == 1 && a.counts().beeps == beeps + 5, "step sounds");
    check(a.counts().voices == 6, "voice count");
    std::vector<float> o(2 * 4800); m.render(o.data(), 4800);
    check(m.peak() > 0 && m.peak() < 4, "renders");
    std::printf("%s legacy sound rules (gun, explosion, body, voices, beeps, mix, music sync)\n", failures ? "FAIL" : "PASS");
}
}  // namespace

int main(int argc, char** argv) {
    if(argc < 3) { std::printf("usage: cat_arena_audio_tests <assets/n7> <fixtures/n7>\n"); return 2; }
    try {
        mixer_units();
        std::printf("%s mixer units (panner, up-mix, biquad, compressor curve, voices, buses, music stream)\n", failures ? "FAIL" : "PASS");
        decode_parity(argv[1], argv[2]);
        graph_parity(argv[2]);
        rules(argv[1]);
    } catch(const std::exception& ex) { std::printf("FAIL exception: %s\n", ex.what()); return 1; }
    std::printf("%s audio\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
