// N7 mixer: the legacy WebAudio graph (sources -> filters -> gain -> panner -> bus gain ->
// compressor (weapons + enemies) -> master; music bus -> master) as a block renderer.
#include "cat/audio.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace cat::audio {
namespace {
constexpr double kPi = 3.141592653589793;
}  // namespace

// ---------------------------------------------------------------- biquad (Web Audio API 1.0, BiquadFilterNode)
Biquad Biquad::make(const Filter& f, double sample_rate) {
    Biquad b;
    const double nyquist = sample_rate / 2;
    const double freq = std::clamp(f.frequency, 0.0, nyquist);
    const double w0 = 2 * kPi * freq / sample_rate, cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2 * std::pow(10.0, f.q / 20));   // lowpass / highpass: Q is in dB
    double b0, b1, b2, a0 = 1 + alpha, a1 = -2 * cw, a2 = 1 - alpha;
    if(f.type == FilterType::lowpass) { b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; }
    else { b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; }
    b.b0 = b0 / a0; b.b1 = b1 / a0; b.b2 = b2 / a0; b.a1 = a1 / a0; b.a2 = a2 / a0;
    return b;
}
double Biquad::process(double x) {
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x; y2 = y1; y1 = y;
    return y;
}

// ---------------------------------------------------------------- compressor (Chromium DynamicsCompressorKernel)
namespace {
constexpr int kMaxPreDelay = 1024, kPreDelayMask = kMaxPreDelay - 1, kDivision = 32;
float lin2db(float x) { return 20 * std::log10(x); }
float db2lin(float d) { return std::pow(10.f, .05f * d); }
}  // namespace
float Compressor::knee_curve(float x, float k) const {   // exponential knee, 1st derivative matched at the threshold
    if(x < linear_threshold_) return x;
    return linear_threshold_ + (1 - std::exp(-k * (x - linear_threshold_))) / k;
}
float Compressor::saturate(float x, float k) const {
    if(x < knee_threshold_) return knee_curve(x, k);
    return db2lin(yknee_threshold_db_ + slope_ * (lin2db(x) - knee_threshold_db_));   // constant ratio after the knee
}
float Compressor::slope_at(float x, float k) const {
    if(x < linear_threshold_) return 1;
    const float x2 = x * 1.001f;
    const float xdb = lin2db(x), x2db = lin2db(x2), ydb = lin2db(knee_curve(x, k)), y2db = lin2db(knee_curve(x2, k));
    return (y2db - ydb) / (x2db - xdb);
}
float Compressor::k_at_slope(float desired) const {
    const float x = db2lin(db_threshold_ + db_knee_);
    float min_k = .1f, max_k = 10000, k = 5;
    for(int i = 0; i < 15; ++i) {
        if(slope_at(x, k) < desired) max_k = k; else min_k = k;
        k = std::sqrt(min_k * max_k);
    }
    return k;
}
Compressor::Compressor(double sample_rate, double threshold_db, double knee_db, double ratio, double attack, double release)
    : rate_(static_cast<float>(sample_rate)), db_threshold_(static_cast<float>(threshold_db)), db_knee_(static_cast<float>(knee_db)), ratio_(static_cast<float>(ratio)) {
    linear_threshold_ = db2lin(db_threshold_);
    slope_ = 1 / ratio_;
    knee_threshold_db_ = db_threshold_ + db_knee_;   // updateStaticCurveParameters()
    knee_threshold_ = db2lin(knee_threshold_db_);
    k_ = 0;
    k_ = k_at_slope(slope_);
    yknee_threshold_db_ = lin2db(knee_curve(knee_threshold_, k_));
    master_gain_ = std::pow(1 / saturate(1, k_), .6f);   // full-range makeup gain
    attack_frames_ = std::max(.001f, static_cast<float>(attack)) * rate_;
    const float release_frames = rate_ * static_cast<float>(release);
    sat_release_frames_ = .0025f * rate_;
    const float y1 = release_frames * .09f, y2 = release_frames * .16f, y3 = release_frames * .42f, y4 = release_frames * .98f;
    ka_ = .9999999999999998f * y1 + 1.8432219684323923e-16f * y2 - 1.9373394351676423e-16f * y3 + 8.824516011816245e-18f * y4;
    kb_ = -1.5788320352845888f * y1 + 2.3305837032074286f * y2 - .9141194204840429f * y3 + .1623677525612032f * y4;
    kc_ = .5334142869106424f * y1 - 1.272736789213631f * y2 + .9258856042207512f * y3 - .18656310191776226f * y4;
    kd_ = .08783463138207234f * y1 - .1694162967925622f * y2 + .08588057951595272f * y3 - .00429891410546283f * y4;
    ke_ = -.042416883008123074f * y1 + .1115693827987602f * y2 - .09764676325265872f * y3 + .028494263462021576f * y4;
    delay_l_.assign(kMaxPreDelay, 0.f); delay_r_.assign(kMaxPreDelay, 0.f);
    const int pre_delay = static_cast<int>(.006f * rate_);
    read_ = (write_ - pre_delay) & kPreDelayMask;
}
double Compressor::curve_db(double input_db) const { return lin2db(saturate(db2lin(static_cast<float>(input_db)), k_)); }
double Compressor::makeup_db() const { return lin2db(master_gain_); }
double Compressor::reduction_db() const { return lin2db(std::sin(1.5707963267948966f * compressor_gain_)); }
void Compressor::process(float* s, std::size_t frames) {
    constexpr float half_pi = 1.5707963267948966f;
    for(std::size_t i = 0; i < frames; ++i) {
        if(division_ == 0) {   // once per 32 frames: desired gain and envelope rate
            if(std::isnan(detector_average_) || std::isinf(detector_average_)) detector_average_ = 1;
            scaled_desired_gain_ = std::asin(detector_average_) / half_pi;
            const bool releasing = scaled_desired_gain_ > compressor_gain_;
            float diff_db = lin2db(compressor_gain_ / scaled_desired_gain_);
            if(releasing) {
                max_attack_diff_db_ = -1;
                if(std::isnan(diff_db) || std::isinf(diff_db)) diff_db = -1;
                float x = std::min(0.f, std::max(-12.f, diff_db));
                x = .25f * (x + 12);
                const float x2 = x * x, x3 = x2 * x, x4 = x2 * x2;
                const float release_frames = ka_ + kb_ * x + kc_ * x2 + kd_ * x3 + ke_ * x4;
                envelope_rate_ = db2lin(5 / release_frames);
            } else {
                if(std::isnan(diff_db) || std::isinf(diff_db)) diff_db = 1;
                if(max_attack_diff_db_ == -1 || max_attack_diff_db_ < diff_db) max_attack_diff_db_ = diff_db;
                const float eff = std::max(.5f, max_attack_diff_db_);
                envelope_rate_ = 1 - std::pow(.25f / eff, 1 / attack_frames_);
            }
        }
        division_ = (division_ + 1) % kDivision;
        delay_l_[static_cast<std::size_t>(write_)] = s[2 * i];
        delay_r_[static_cast<std::size_t>(write_)] = s[2 * i + 1];
        const float input = std::max(std::fabs(s[2 * i]), std::fabs(s[2 * i + 1]));
        const float shaped = saturate(input, k_);
        const float attenuation = input <= .0001f ? 1 : shaped / input;
        const float attenuation_db = std::max(2.f, -lin2db(attenuation));
        const float sat_release_rate = db2lin(attenuation_db / sat_release_frames_) - 1;
        const float rate = attenuation > detector_average_ ? sat_release_rate : 1;
        detector_average_ = std::min(1.f, detector_average_ + (attenuation - detector_average_) * rate);
        if(std::isnan(detector_average_) || std::isinf(detector_average_)) detector_average_ = 1;
        if(envelope_rate_ < 1) compressor_gain_ += (scaled_desired_gain_ - compressor_gain_) * envelope_rate_;
        else compressor_gain_ = std::min(1.f, compressor_gain_ * envelope_rate_);
        const float gain = master_gain_ * std::sin(half_pi * compressor_gain_);
        s[2 * i] = delay_l_[static_cast<std::size_t>(read_)] * gain;
        s[2 * i + 1] = delay_r_[static_cast<std::size_t>(read_)] * gain;
        read_ = (read_ + 1) & kPreDelayMask;
        write_ = (write_ + 1) & kPreDelayMask;
    }
}

// ---------------------------------------------------------------- mixer
namespace {
constexpr int kSincHalf = 16, kSincSteps = 512;
struct SincTable {
    double cutoff;
    std::vector<float> taps;   // kernel at x = k / kSincSteps for k in [0, reach * kSincSteps]
    double reach;
    explicit SincTable(double c) : cutoff(c), reach(kSincHalf / c) {
        const auto n = static_cast<std::size_t>(std::ceil(reach * kSincSteps)) + 2;
        taps.resize(n);
        for(std::size_t k = 0; k < n; ++k) {
            const double x = double(k) / kSincSteps;
            if(x >= reach) { taps[k] = 0; continue; }
            const double t = x * cutoff, s = t == 0 ? 1 : std::sin(kPi * t) / (kPi * t), w = x / reach;
            taps[k] = static_cast<float>(s * (.42 + .5 * std::cos(kPi * w) + .08 * std::cos(2 * kPi * w)) * cutoff);
        }
    }
    float at(double x) const {
        x = std::fabs(x) * kSincSteps;
        const auto k = static_cast<std::size_t>(x);
        if(k + 1 >= taps.size()) return 0;
        const auto f = static_cast<float>(x - double(k));
        return taps[k] + (taps[k + 1] - taps[k]) * f;
    }
};
// Fourier series of OscillatorNode's basic waveforms (Chromium PeriodicWave::GenerateBasicWaveform).
double harmonic(Wave w, int n) {
    switch(w) {
        case Wave::sine: return n == 1 ? 1 : 0;
        case Wave::square: return (n & 1) ? 4 / (n * kPi) : 0;
        case Wave::sawtooth: return ((n & 1) ? 2 : -2) / (n * kPi);
        case Wave::triangle: return (n & 1) ? 8 / (kPi * kPi * n * n) * (((n - 1) / 2) & 1 ? -1 : 1) : 0;
    }
    return 0;
}
double wave_sample(Wave w, double phase, int harmonics) {   // sum b_n sin(n phase) by recurrence
    if(w == Wave::sine) return std::sin(phase);
    const double c2 = 2 * std::cos(phase);
    double s_prev = 0, s = std::sin(phase), sum = 0;
    for(int n = 1; n <= harmonics; ++n) {
        sum += harmonic(w, n) * s;
        const double next = c2 * s - s_prev;
        s_prev = s; s = next;
    }
    return sum;
}
}  // namespace

Mixer::Mixer(int rate) : rate_(rate), compressor_(rate) {
    if(rate < 8000 || rate > 384000) throw std::runtime_error("Mixer: unsupported sample rate");
}
void Mixer::play(const Voice& v) {
    if(!v.buffer || v.buffer->frames() == 0) return;
    Playing p{v, 0, {}, 0, 0};
    const std::size_t channels = static_cast<std::size_t>(std::min(v.buffer->channels, 2));
    for(std::size_t c = 0; c < channels; ++c) for(const auto& f : v.filters) p.filters.push_back(Biquad::make(f, rate_));
    std::lock_guard lock(mutex_);
    voices_.push_back(std::move(p));
}
void Mixer::tone(const Tone& t) {
    Playing_tone p{t, 0, 0, 1, 1};
    if(t.wave != Wave::sine) {   // normalise to a peak of 1 (PeriodicWave normalisation), with the harmonics of the start frequency
        p.harmonics = std::max(1, static_cast<int>(rate_ / 2 / std::max(1.0, t.frequency)));
        double peak = 0;
        for(int i = 0; i < 2048; ++i) peak = std::max(peak, std::fabs(wave_sample(t.wave, 2 * kPi * i / 2048, p.harmonics)));
        p.norm = peak > 0 ? 1 / peak : 1;
    }
    std::lock_guard lock(mutex_);
    tones_.push_back(p);
}
void Mixer::set_master(double g) { std::lock_guard lock(mutex_); master_ = g; }
void Mixer::set_bus(Bus b, double g) { std::lock_guard lock(mutex_); bus_gain_[static_cast<std::size_t>(b)] = g; }
void Mixer::set_compressor(bool on) { std::lock_guard lock(mutex_); compressor_on_ = on; }
void Mixer::set_music(std::unique_ptr<Stream> s) {
    std::lock_guard lock(mutex_);
    music_ = std::move(s);
    music_in_.assign(static_cast<std::size_t>(2 * (kSincHalf * 2 + 2)), 0.f);   // leading silence for the filter history
    music_pos_ = kSincHalf * 2;
}
void Mixer::set_music_playing(bool on) { std::lock_guard lock(mutex_); music_playing_ = on && music_ != nullptr; }
bool Mixer::music_playing() const { std::lock_guard lock(mutex_); return music_playing_; }
int Mixer::active(int tag) const {
    std::lock_guard lock(mutex_);
    return static_cast<int>(std::count_if(voices_.begin(), voices_.end(), [&](const Playing& p) { return p.v.tag == tag; }));
}
std::size_t Mixer::voices() const { std::lock_guard lock(mutex_); return voices_.size() + tones_.size(); }
double Mixer::peak() const { std::lock_guard lock(mutex_); return peak_; }

void Mixer::mix_voice(Playing& p, float* out, std::size_t frames, bool& done) {
    const Buffer& b = *p.v.buffer;
    const std::size_t n = b.frames(), channels = static_cast<std::size_t>(b.channels), filters = p.v.filters.size();
    const double bus = bus_gain_[static_cast<std::size_t>(p.v.bus)], gain = p.v.gain * bus;
    double pl = 1, pr = 1;
    if(p.v.pan) {   // StereoPannerNode, equal power
        const double pan = std::clamp(*p.v.pan, -1.0, 1.0);
        if(channels == 1) { const double x = (pan + 1) / 2; pl = std::cos(x * kPi / 2); pr = std::sin(x * kPi / 2); }
        else { const double x = pan <= 0 ? pan + 1 : pan; pl = std::cos(x * kPi / 2); pr = std::sin(x * kPi / 2); }
    }
    for(std::size_t i = 0; i < frames; ++i) {
        const auto k = static_cast<std::size_t>(p.pos);
        if(k >= n && (filters == 0 || p.quiet >= 64 || p.tail >= rate_ / 2)) { done = true; return; }   // filters ring on after the source ends (node tail time)
        const double f = p.pos - double(k);
        double s[2];
        double loudest = 0;
        for(std::size_t c = 0; c < std::min<std::size_t>(channels, 2); ++c) {
            double v = 0;
            if(k < n) {   // linear interpolation (AudioBufferSourceNode); past the last frame towards silence
                const double a = b.samples[k * channels + c], next = k + 1 < n ? b.samples[(k + 1) * channels + c] : 0;
                v = a + (next - a) * f;
            }
            for(std::size_t q = 0; q < filters; ++q) v = p.filters[c * filters + q].process(v);
            loudest = std::max(loudest, std::fabs(v));
            s[c] = v * gain;
        }
        if(k >= n) { ++p.tail; p.quiet = loudest < 1e-7 ? p.quiet + 1 : 0; }
        double l, r;
        if(channels == 1) { l = s[0] * pl; r = s[0] * pr; }
        else if(!p.v.pan) { l = s[0]; r = s[1]; }
        else if(*p.v.pan <= 0) { l = s[0] + s[1] * pl; r = s[1] * pr; }
        else { l = s[0] * pl; r = s[1] + s[0] * pr; }
        out[2 * i] += static_cast<float>(l); out[2 * i + 1] += static_cast<float>(r);
        p.pos += p.v.rate;
    }
}

void Mixer::mix_tone(Playing_tone& p, float* out, std::size_t frames, bool& done) {
    const Tone& t = p.t;
    const double bus = bus_gain_[static_cast<std::size_t>(t.bus)], dt = 1.0 / rate_;
    for(std::size_t i = 0; i < frames; ++i) {
        if(p.elapsed >= t.length) { done = true; return; }
        const double u = p.elapsed / t.length;
        const double freq = t.frequency * std::pow(t.frequency_end / t.frequency, u), g = t.gain * std::pow(t.gain_end / t.gain, u);
        const int harmonics = t.wave == Wave::sine ? 1 : std::max(1, static_cast<int>(rate_ / 2 / freq));
        const double v = wave_sample(t.wave, p.phase, harmonics) * p.norm * g * bus;
        out[2 * i] += static_cast<float>(v); out[2 * i + 1] += static_cast<float>(v);
        p.phase = std::fmod(p.phase + 2 * kPi * freq * dt, 2 * kPi);
        p.elapsed += dt;
    }
}

void Mixer::mix_music(float* out, std::size_t frames) {
    if(!music_ || !music_playing_) return;
    const int in_rate = music_->rate();
    const double step = double(in_rate) / rate_;
    thread_local std::map<long long, SincTable> tables;   // by cutoff (1e-6 steps); built once per rate pair
    const double cutoff = step > 1 ? .97 / step : .97;
    const auto key = static_cast<long long>(std::llround(cutoff * 1e6));
    const SincTable* table = &tables.try_emplace(key, cutoff).first->second;
    const double reach = table->reach;
    const auto need = static_cast<std::size_t>(std::ceil(music_pos_ + double(frames) * step + reach)) + 2;
    while(music_in_.size() / 2 < need) {
        const std::size_t have = music_in_.size() / 2, want = need - have + 1024;
        music_in_.resize((have + want) * 2);
        std::size_t got = music_->read(&music_in_[have * 2], want);
        if(got < want) {   // loop: continue from the start
            music_->rewind();
            got += music_->read(&music_in_[(have + got) * 2], want - got);
        }
        music_in_.resize((have + got) * 2);
        if(got == 0) return;
    }
    const double bus = bus_gain_[static_cast<std::size_t>(Bus::music)];
    for(std::size_t i = 0; i < frames; ++i) {
        const double centre = music_pos_;
        const auto first = static_cast<long long>(std::ceil(centre - reach)), last = static_cast<long long>(std::floor(centre + reach));
        double l = 0, r = 0;
        for(long long k = std::max(0LL, first); k <= last; ++k) {
            const float w = table->at(centre - double(k));
            l += music_in_[static_cast<std::size_t>(k) * 2] * w; r += music_in_[static_cast<std::size_t>(k) * 2 + 1] * w;
        }
        out[2 * i] += static_cast<float>(l * bus); out[2 * i + 1] += static_cast<float>(r * bus);
        music_pos_ += step;
    }
    const auto drop = static_cast<std::size_t>(std::max(0.0, std::floor(music_pos_ - reach) - 1));
    if(drop > 0) { music_in_.erase(music_in_.begin(), music_in_.begin() + static_cast<std::ptrdiff_t>(drop * 2)); music_pos_ -= double(drop); }
}

void Mixer::render(float* out, std::size_t frames) {
    std::lock_guard lock(mutex_);
    scratch_fx_.assign(frames * 2, 0.f);
    scratch_music_.assign(frames * 2, 0.f);
    for(std::size_t i = voices_.size(); i-- > 0;) {
        bool done = false;
        mix_voice(voices_[i], scratch_fx_.data(), frames, done);
        if(done) voices_.erase(voices_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    for(std::size_t i = tones_.size(); i-- > 0;) {
        bool done = false;
        mix_tone(tones_[i], scratch_fx_.data(), frames, done);
        if(done) tones_.erase(tones_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    if(compressor_on_) compressor_.process(scratch_fx_.data(), frames);
    mix_music(scratch_music_.data(), frames);
    peak_ = 0;
    for(std::size_t i = 0; i < frames * 2; ++i) {
        const double v = (double(scratch_fx_[i]) + scratch_music_[i]) * master_;
        peak_ = std::max(peak_, std::fabs(v));
        out[i] = static_cast<float>(std::clamp(v, -1.0, 1.0));
    }
    rendered_ += frames;
}

}  // namespace cat::audio
