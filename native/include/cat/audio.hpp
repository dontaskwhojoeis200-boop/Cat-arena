#pragma once
// N7 audio: decoding, a WebAudio-style mixer and the legacy game's sound rules. No SDL here; the
// desktop shell feeds Mixer::render() to an SDL audio stream (src/audio_device.cpp), tests call it
// directly (offline, deterministic).
//
// Legacy (index.html 11604-11800): an AudioContext with a master bus and three buses (weapons,
// enemies, music); weapons and enemies pass a DynamicsCompressor (threshold -10 dB, knee 12, ratio
// 3.1, attack 8 ms, release 160 ms); samples are AudioBufferSourceNodes with playbackRate, a gain
// and optionally a StereoPanner and biquad filters; beeps are oscillators with exponential ramps; the
// soundtrack is a looping MP3 media element on the music bus, paused while the page is hidden.
#include "cat/combat.hpp"
#include "cat/physics.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace cat::audio {

// Interleaved float PCM.
struct Buffer {
    int rate{48000};
    int channels{1};
    std::vector<float> samples;
    std::size_t frames() const { return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0; }
    double seconds() const { return rate > 0 ? double(frames()) / rate : 0; }
};

Buffer decode_ogg(const std::vector<std::uint8_t>& bytes);   // stb_vorbis; throws on bad data
Buffer decode_mp3(const std::vector<std::uint8_t>& bytes);   // dr_mp3, whole file (tests)
std::vector<std::uint8_t> read_file(const std::filesystem::path& path);
// Band-limited (windowed-sinc) sample-rate conversion, as decodeAudioData converts to the context rate.
Buffer resample(const Buffer& in, int rate);

// Pull source for the soundtrack: stereo frames at its own rate.
class Stream {
public:
    virtual ~Stream() = default;
    virtual int rate() const = 0;
    virtual std::size_t read(float* stereo, std::size_t frames) = 0;   // fewer frames = end reached
    virtual void rewind() = 0;
};
std::unique_ptr<Stream> open_mp3(std::vector<std::uint8_t> bytes);     // streaming dr_mp3 decoder

enum class Bus : std::uint8_t { weapon, enemy, music };
enum class Wave : std::uint8_t { sine, square, sawtooth, triangle };
enum class FilterType : std::uint8_t { lowpass, highpass };
struct Filter { FilterType type{FilterType::lowpass}; double frequency{350}; double q{1}; };   // BiquadFilterNode (Q in dB)

struct Voice {
    std::shared_ptr<const Buffer> buffer;   // at the mixer rate (resample() first)
    Bus bus{Bus::weapon};
    double gain{1}, rate{1};                // GainNode.gain, playbackRate (linear interpolation, as Chromium)
    std::optional<double> pan;              // StereoPannerNode.pan; none = no panner (mono is copied to both sides)
    std::vector<Filter> filters;            // applied in order before the gain
    int tag{};                              // voices with a tag are counted by active(tag) (cat voice polyphony)
};
// OscillatorNode + GainNode of beep(): frequency and gain ramp exponentially over `length`, then stop.
struct Tone { Wave wave{Wave::sine}; double frequency{440}, frequency_end{440}, gain{.025}, gain_end{.001}, length{.09}; Bus bus{Bus::weapon}; };

struct Biquad {   // Direct form I, coefficients per the Web Audio API specification
    double b0{1}, b1{}, b2{}, a1{}, a2{};
    double x1{}, x2{}, y1{}, y2{};
    static Biquad make(const Filter& f, double sample_rate);
    double process(double x);
};

// DynamicsCompressorNode with the legacy settings: a port of Chromium's DynamicsCompressorKernel
// (exponential knee fitted to the ratio, 6 ms pre-delay, adaptive release polynomial, sine-warped
// gain, makeup gain (1 / full-range gain)^0.6), evaluated in float like the original.
class Compressor {
public:
    explicit Compressor(double sample_rate, double threshold_db = -10, double knee_db = 12, double ratio = 3.1, double attack = .008, double release = .16);
    void process(float* stereo, std::size_t frames);
    double curve_db(double input_db) const;   // static curve (saturate) in dB
    double makeup_db() const;
    double reduction_db() const;              // current gain (dB, <= 0)
private:
    float knee_curve(float x, float k) const;
    float saturate(float x, float k) const;
    float slope_at(float x, float k) const;
    float k_at_slope(float slope) const;
    float rate_, db_threshold_, db_knee_, ratio_, linear_threshold_{}, slope_{}, knee_threshold_{}, knee_threshold_db_{}, yknee_threshold_db_{}, k_{};
    float master_gain_{}, attack_frames_{}, sat_release_frames_{}, ka_{}, kb_{}, kc_{}, kd_{}, ke_{};
    float detector_average_{0}, compressor_gain_{1}, max_attack_diff_db_{-1};
    float envelope_rate_{1}, scaled_desired_gain_{1};
    std::vector<float> delay_l_, delay_r_;
    int write_{}, read_{}, division_{};
};

class Mixer {
public:
    explicit Mixer(int rate = 48000);
    int rate() const { return rate_; }
    // Thread-safe (the device thread calls render()).
    void play(const Voice& v);
    void tone(const Tone& t);
    void set_master(double gain);
    void set_bus(Bus bus, double gain);
    void set_compressor(bool on);
    void set_music(std::unique_ptr<Stream> stream);   // looped
    void set_music_playing(bool playing);
    bool music_playing() const;
    int active(int tag) const;
    std::size_t voices() const;
    void render(float* stereo, std::size_t frames);
    std::uint64_t rendered_frames() const { return rendered_.load(); }
    double peak() const;   // largest |sample| of the last render() (before clipping)
private:
    struct Playing { Voice v; double pos{}; std::vector<Biquad> filters; int tail{}, quiet{}; };
    struct Playing_tone { Tone t; double phase{}, elapsed{}; double norm{1}; int harmonics{1}; };
    void mix_voice(Playing& p, float* bus, std::size_t frames, bool& done);
    void mix_tone(Playing_tone& p, float* bus, std::size_t frames, bool& done);
    void mix_music(float* bus, std::size_t frames);
    int rate_;
    mutable std::mutex mutex_;
    std::vector<Playing> voices_;
    std::vector<Playing_tone> tones_;
    double master_{1};
    std::array<double, 3> bus_gain_{1, 1, 1};
    bool compressor_on_{true};
    Compressor compressor_;
    std::unique_ptr<Stream> music_;
    bool music_playing_{};
    // music resampler state (windowed sinc, stereo)
    std::vector<float> music_in_;
    double music_pos_{};
    std::vector<float> scratch_fx_, scratch_music_, scratch_one_;
    std::atomic<std::uint64_t> rendered_{};
    double peak_{};
};

// The 48 samples of VOID_V318_SAMPLE_BANK (assets/n7/audio/sfx), by legacy key ("gun4:1", "cat:hurt:2", ...).
class SampleBank {
public:
    // Reads bank.txt and every Ogg file, decodes and converts to `rate`. Throws if a file is missing.
    static SampleBank load(const std::filesystem::path& dir, int rate);
    std::shared_ptr<const Buffer> get(const std::string& key) const;
    std::size_t size() const { return samples_.size(); }
    const std::map<std::string, std::shared_ptr<const Buffer>>& all() const { return samples_; }
    void add(const std::string& key, Buffer b) { samples_[key] = std::make_shared<const Buffer>(std::move(b)); }
private:
    std::map<std::string, std::shared_ptr<const Buffer>> samples_;
};

struct MixSettings {   // the legacy audio settings tab (percent)
    int master{70}, weapons{85}, enemies{80}, music{46};
    bool music_enabled{true};
};
struct Listener { double x{}, y{}, a{}; };

// The legacy sound rules (playGunSound, playExplosionSound, playBodySound, playEnemyVoice, beep,
// ensureThemeMusic / syncThemePlayback). Own random stream: unlike the legacy, where these calls draw
// from Math.random() and so change the gameplay sequence once audio has started, sound never touches
// the simulation's generator.
class GameAudio {
public:
    GameAudio(Mixer& mixer, std::shared_ptr<const SampleBank> bank, std::uint32_t seed = 0x5eed);
    void apply(const MixSettings& s);
    // audiostart(): sound is enabled on the first deliberate action (PLAY, ENTER, TEST AUDIO), as in the legacy.
    void start();
    bool started() const { return started_; }
    void set_visible(bool visible);           // document.hidden: the soundtrack pauses while the window is hidden / in the background
    void set_in_game(bool in_game);           // returnMenu() pauses the soundtrack; launching a game resumes it
    void gun(int weapon);
    void explosion(double power);
    void body(physics::BodySound kind, double force);
    bool voice(const combat::VoiceEvent& e, const Listener& listener, double now);   // false = suppressed by the legacy limits
    void beep(double frequency, double length = .09, Wave wave = Wave::sawtooth, double volume = .025);
    void test();                              // TEST AUDIO: beep(410, .14, 'sawtooth', .08)
    // One simulation step's sounds, in the legacy call order.
    void step(const combat::Events& e, const std::vector<physics::BodyEvent>& body, const Listener& listener, double now);
    struct Counts { std::uint64_t guns{}, explosions{}, bodies{}, voices{}, voices_suppressed{}, beeps{}; };
    const Counts& counts() const { return counts_; }
    combat::Rng& rng() { return rng_; }
private:
    void sync_music();
    std::shared_ptr<const Buffer> synth_body(physics::BodySound kind);
    Mixer* mixer_;
    std::shared_ptr<const SampleBank> bank_;
    combat::Rng rng_;
    MixSettings settings_;
    bool started_{}, visible_{true}, in_game_{};
    std::array<std::uint16_t, 16> gun_take_{};
    std::map<int, std::shared_ptr<const Buffer>> body_cache_;
    std::map<std::uint32_t, double> last_meow_;
    double voice_global_at_{-10};
    std::array<double, 4> voice_last_{-10, -10, -10, -10};
    Counts counts_;
};

// Legacy enemy type names (for the voice families of playEnemyVoice).
const char* enemy_type_name(int type);

}  // namespace cat::audio
