// N7: the legacy sound rules (index.html 11604-11800) on top of cat::audio::Mixer.
#include "cat/audio.hpp"
#include "cat/enemies.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace cat::audio {
namespace {
constexpr int kCatVoiceTag = 1;
constexpr double kPi = 3.141592653589793;
}  // namespace

const char* enemy_type_name(int type) {
    const auto& t = combat::enemy_types();
    return type >= 0 && static_cast<std::size_t>(type) < t.size() ? t[static_cast<std::size_t>(type)].name : "";
}

SampleBank SampleBank::load(const std::filesystem::path& dir, int rate) {
    std::ifstream list(dir / "bank.txt");
    if(!list) throw std::runtime_error("Missing sample bank list " + (dir / "bank.txt").string());
    SampleBank bank;
    std::string line;
    while(std::getline(list, line)) {
        if(line.empty()) continue;
        std::istringstream row(line);
        std::string key, file, length;
        if(!std::getline(row, key, '\t') || !std::getline(row, file, '\t')) throw std::runtime_error("Malformed sample bank line: " + line);
        Buffer b = decode_ogg(read_file(dir / file));
        if(std::getline(row, length, '\t') && !length.empty()) {   // the length the browser decodes (see tools/n7/set_browser_lengths.py)
            const auto frames = static_cast<std::size_t>(std::stoul(length));
            if(frames < b.frames()) b.samples.resize(frames * static_cast<std::size_t>(b.channels));
        }
        bank.add(key, resample(b, rate));
    }
    if(bank.size() != 48) throw std::runtime_error("Sample bank: expected the 48 legacy samples, found " + std::to_string(bank.size()));
    return bank;
}
std::shared_ptr<const Buffer> SampleBank::get(const std::string& key) const {
    const auto it = samples_.find(key);
    return it == samples_.end() ? nullptr : it->second;
}

GameAudio::GameAudio(Mixer& mixer, std::shared_ptr<const SampleBank> bank, std::uint32_t seed) : mixer_(&mixer), bank_(std::move(bank)), rng_(seed) { apply(settings_); }

// setAudioMix()
void GameAudio::apply(const MixSettings& s) {
    settings_ = s;
    mixer_->set_master(std::clamp(s.master, 0, 100) / 100.0);
    mixer_->set_bus(Bus::weapon, std::clamp(s.weapons, 0, 100) / 100.0);
    mixer_->set_bus(Bus::enemy, std::clamp(s.enemies, 0, 100) / 100.0);
    mixer_->set_bus(Bus::music, s.music_enabled ? std::clamp(s.music, 0, 100) / 100.0 : 0);
    sync_music();
}
void GameAudio::start() { started_ = true; in_game_ = true; sync_music(); }
void GameAudio::set_visible(bool v) { visible_ = v; sync_music(); }
void GameAudio::set_in_game(bool g) { if(g) start(); else { in_game_ = false; sync_music(); } }
// syncThemePlayback(): plays while enabled and the page is visible; returnMenu() pauses it.
void GameAudio::sync_music() { mixer_->set_music_playing(started_ && in_game_ && settings_.music_enabled && visible_); }

// playGunSound(index) (11727): two takes per weapon, alternating with a random skip.
void GameAudio::gun(int index) {
    if(!started_ || index < 0 || index > 15) return;
    const int take = (gun_take_[static_cast<std::size_t>(index)]++ + (rng_.next() > .57 ? 1 : 0)) & 1;
    const bool heavy = index == 1 || index == 4 || index == 7 || index == 12 || index == 15, rapid = index == 0 || index == 5 || index == 11 || index == 14;
    const double level = heavy ? .50 : rapid ? .38 : .42, rate = .987 + rng_.next() * .027;
    auto buf = bank_->get("gun" + std::to_string(index) + ":" + std::to_string(take));
    if(!buf) buf = bank_->get("gun" + std::to_string(index) + ":" + std::to_string(1 - take));
    if(!buf) return;
    mixer_->play({buf, Bus::weapon, level, rate, std::nullopt, {}, 0});
    ++counts_.guns;
}

// playExplosionSound(power) (11744)
void GameAudio::explosion(double power) {
    if(!started_) return;
    const double level = std::min(.70, .34 * power), rate = .94 + rng_.next() * .09;
    if(auto buf = bank_->get("explosion")) { mixer_->play({buf, Bus::weapon, level, rate, std::nullopt, {}, 0}); ++counts_.explosions; }
}

// synthNoiseBuffer() (11752): the procedural footfall; the bank has no "body:step" sample, so the
// legacy always falls back to this recipe for steps (and for any body sound whose sample is missing).
std::shared_ptr<const Buffer> GameAudio::synth_body(physics::BodySound kind) {
    const int key = static_cast<int>(kind);
    if(auto it = body_cache_.find(key); it != body_cache_.end()) return it->second;
    const bool slide = kind == physics::BodySound::slide, landing = kind == physics::BodySound::land;
    const double duration = slide ? .27 : landing ? .22 : .17;
    const int sr = mixer_->rate();
    const auto n = static_cast<std::size_t>(std::max(1.0, std::floor(sr * duration)));
    Buffer b;
    b.rate = sr; b.channels = 1; b.samples.resize(n);
    double low = 0, mid = 0, phase = 0;
    for(std::size_t i = 0; i < n; ++i) {
        const double t = double(i) / sr, w = rng_.next() * 2 - 1;
        low += (w - low) * .034; mid += (w - mid) * .23;
        auto tone = [&] { phase += 2 * kPi * (90 - 60 * t / duration) / sr; return std::sin(phase); };
        const double v = slide ? (w * .13 + mid * .76 + low * .22) * std::exp(-t * 11)
                       : landing ? (low * 2.0 + mid * .75 + tone() * .30) * std::exp(-t * 26)
                                 : (w * .20 + mid * .65 + tone() * .50) * std::exp(-t * 31);
        b.samples[i] = static_cast<float>(std::tanh(v) * std::min(1.0, t * 1500) * std::min(1.0, (duration - t) * 30));
    }
    auto shared = std::make_shared<const Buffer>(std::move(b));
    body_cache_[key] = shared;
    return shared;
}

// playBodySound(kind, force) (11761)
void GameAudio::body(physics::BodySound kind, double force) {
    if(!started_) return;
    static const char* const names[] = {"jump", "land", "step", "slide"};
    const bool landing = kind == physics::BodySound::land;
    const double rate = .95 + rng_.next() * .08;   // drawn for the sample attempt
    if(auto buf = bank_->get(std::string("body:") + names[static_cast<int>(kind)])) {
        mixer_->play({buf, Bus::weapon, (landing ? .24 : .16) * force, rate, std::nullopt, {}, 0});
    } else {
        mixer_->play({synth_body(kind), Bus::weapon, (landing ? .23 : .12) * force, 1, std::nullopt, {}, 0});
    }
    ++counts_.bodies;
}

// playEnemyVoice(b, kind) (11770): distance, polyphony and cooldown limits, three takes, family tone,
// filters, attenuation and stereo position.
bool GameAudio::voice(const combat::VoiceEvent& e, const Listener& p, double now) {
    if(!started_) return false;
    const bool death = e.kind == combat::VoiceKind::eliminated;
    const double d = std::hypot(e.x - p.x, e.y - p.y);
    const int playing = mixer_->active(kCatVoiceTag);
    if(d > 31 || (!death && playing >= 3) || playing >= 5) { ++counts_.voices_suppressed; return false; }
    const double per_enemy = death ? 0 : e.kind == combat::VoiceKind::attack ? .42 : e.kind == combat::VoiceKind::hurt ? .28 : 1.8;
    const auto last = last_meow_.find(e.id);
    const double last_at = last == last_meow_.end() ? -10 : last->second;
    const auto k = static_cast<std::size_t>(e.kind);
    if((!death && now < last_at + per_enemy) || (!death && now - voice_global_at_ < .095) ||
       (e.kind == combat::VoiceKind::idle && now - voice_last_[k] < 1.15) || (e.kind == combat::VoiceKind::hurt && now - voice_last_[k] < .10)) {
        ++counts_.voices_suppressed;
        return false;
    }
    last_meow_[e.id] = now; voice_last_[k] = now; voice_global_at_ = now;
    if(last_meow_.size() > 512) last_meow_.clear();
    const std::string type = enemy_type_name(e.type);
    const bool bass = type == "shambler" || type == "crusher" || type == "brute" || type == "warden" || type == "skyjuggernaut";
    const bool light = type == "fiend" || type == "skitter" || type == "stalker" || type == "banshee" || type == "harrier" || type == "skyprobe";
    const double tone = bass ? .92 : light ? 1.025 : 1.0;
    const int index = static_cast<int>(std::floor(rng_.next() * 3));
    static const char* const kinds[] = {"idle", "attack", "hurt", "eliminated"};
    const auto buf = bank_->get(std::string("cat:") + kinds[k] + ":" + std::to_string(index));
    if(!buf) return false;
    const double rate = tone * (.986 + rng_.next() * .026);
    const double attenuation = std::pow(std::max(0.0, 1 - d / 35), 1.1);
    const double base = e.kind == combat::VoiceKind::attack ? .66 : e.kind == combat::VoiceKind::hurt ? .60 : death ? .74 : .31;
    const double ang = std::atan2(e.y - p.y, e.x - p.x) - p.a;
    Voice v{buf, Bus::enemy, base * attenuation, rate, std::clamp(std::sin(ang) * .70, -.72, .72),
            {Filter{FilterType::lowpass, light ? 4600.0 : bass ? 3700.0 : 4100.0, 1}, Filter{FilterType::highpass, bass ? 130.0 : 155.0, 1}}, kCatVoiceTag};
    mixer_->play(v);
    ++counts_.voices;
    return true;
}

// beep(f, len, wave, volume) (11641)
void GameAudio::beep(double f, double len, Wave wave, double volume) {
    if(!started_) return;
    mixer_->tone({wave, f, std::max(45.0, f * .49), volume, .001, len, Bus::weapon});
    ++counts_.beeps;
}
void GameAudio::test() { start(); beep(410, .14, Wave::sawtooth, .08); }

void GameAudio::step(const combat::Events& e, const std::vector<physics::BodyEvent>& bodies, const Listener& listener, double now) {
    if(!started_) return;
    for(const auto& b : bodies) body(b.kind, b.force);
    if(e.switched) beep(700, .07, Wave::sine, .015);                 // switchWeapon()
    if(e.reload_started) beep(350, .08, Wave::triangle, .019);       // reloadWeapon()
    if(e.fired_weapon >= 0) gun(e.fired_weapon);                     // fire()
    for(const auto& b : e.blasts) explosion(b.power);                // explode()
    for(const auto& v : e.voices) voice(v, listener, now);
    for(std::uint32_t i = 0; i < e.player_hits; ++i) beep(100, .08, Wave::sawtooth, .013);   // takeDamage()
    if(e.healed > 0) beep(830, .14, Wave::sine, .022);              // medkit pickup
}

}  // namespace cat::audio
